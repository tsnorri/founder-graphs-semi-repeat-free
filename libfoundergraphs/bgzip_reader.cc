/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <array>
#include <boost/endian/conversion.hpp>
#include <boost/format.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <cstring>
#include <founder_graphs/bgzip_reader.hh>
#include <founder_graphs/utility.hh>
#include <libbio/file_handling.hh>

namespace lb		= libbio;
namespace ios		= boost::iostreams;
namespace endian	= boost::endian;


namespace {
	
	// Helper for filling BGZIP_EOF_MARKER.
	consteval char operator""_c(unsigned long long const val)
	{
		// User-defined literals can only have certain parameter types; in particular plain int is not allowed.
		// Start by checking that the passed unsigned long long is small enough.
		// static_assert cannot be used here b.c. val is not constexpr. (Currently no function parameters can be.)
		do
		{
			if ((0xffULL & val) != val)
				throw "Bounds check failed.";
		} while (false);
		
		// We would like to handle both signed and unsigned char here.
		// Clang++ 13’s libc++ does not have std::bit_cast and std::memcpy
		// is not constexpr. To mitigate, we use bit shifts to get
		// the sign bit to its place, and then bitwise-or the shifted bit back.
		char retval{char((0xffULL & val) >> 1)};
		retval <<= 1; // Defined behaviour even for signed char b.c. the sign bit is zero before the shift.
		retval |= (val & 0x1);
		return retval;
	}
	
	
	constexpr static auto const BGZIP_EOF_MARKER{lb::make_array <char>(
		0x1f_c, 0x8b_c, 0x08_c, 0x04_c, 0x00_c, 0x00_c, 0x00_c, 0x00_c,
		0x00_c, 0xff_c, 0x06_c, 0x00_c, 0x42_c, 0x43_c, 0x02_c, 0x00_c,
		0x1b_c, 0x00_c, 0x03_c, 0x00_c, 0x00_c, 0x00_c, 0x00_c, 0x00_c,
		0x00_c, 0x00_c, 0x00_c, 0x00_c
	)};
	
	
	inline std::uint64_t read_uint64(lb::file_istream &stream)
	{
		std::uint64_t val{};
		if (!stream.read(reinterpret_cast <char *>(&val), 8))
			throw std::runtime_error("Unable to read from file");
		
		return endian::little_to_native(val);
	}
	
	
	bool check_eof_marker(lb::file_handle const &handle, std::size_t const compressed_size)
	{
		// Make sure the EOF marker is present.
		// Since its contents are defined in the specification, we compare bytes here.
		// See Section 4.1.2. End-of-file marker in https://samtools.github.io/hts-specs/SAMv1.pdf
		constexpr auto const expected_size(BGZIP_EOF_MARKER.size());
		std::array <char, expected_size> buffer;
		// Use pread since it does not modify the file pointer.
		auto const res(::pread(handle.get(), buffer.data(), expected_size, compressed_size - expected_size));
		switch (res)
		{
			// FIXME: consider the semantics; should we always throw  or always return false if the result is unexpected?
			case -1:
				throw std::runtime_error(std::strerror(errno));
				
			case 0:
				throw std::runtime_error("EOF reached while checking for end-of-file marker.");
			
			default:
			{
				if (res != expected_size)
					return false;
				
				return std::equal(buffer.begin(), buffer.end(), BGZIP_EOF_MARKER.begin(), BGZIP_EOF_MARKER.end());
			}
		}
	}
	
	
	std::size_t read_uncompressed_size(std::vector <char> const &input)
	{
		// Read the uncompressed size from the passed buffer.
		auto const size(input.size());
		libbio_always_assert(4 <= size);
		auto const pos(size - 4);
		std::uint32_t val{};
		std::memcpy(&val, input.data() + pos, 4);
		return endian::little_to_native(val);
	}


	std::size_t read_uncompressed_size(lb::file_handle const &handle, std::size_t const next_block_pos)
	{
		// Read the uncompressed size from the handle.
		std::uint32_t val{};
		auto const res(::pread(handle.get(), &val, 4, next_block_pos - 4));
		switch (res)
		{
			case -1:
				throw std::runtime_error(std::strerror(errno));

			case 0:
				throw std::runtime_error("EOF reached while checking the uncompressed size of the last compressed block.");

			default:
				return endian::little_to_native(val);
		}
	}
}


namespace founder_graphs {
	
	void bgzip_reader::open(std::string const &path)
	{
		auto const index_path(boost::str(boost::format("%s.gzi") % path));
		
		// These exit on error.
		lb::file_handle bgzip_handle(lb::open_file_for_reading(path));
		lb::file_handle index_handle(lb::open_file_for_reading(index_path));
		
		this->open(std::move(bgzip_handle), index_handle);
	}
	
	
	void bgzip_reader::open(lb::file_handle &&handle, lb::file_handle &index_handle)
	{
		auto const [compressed_size, preferred_block_size] = check_file_size(handle);
		
		// There is an EOF marker at the end of the file. Check its contents.
		libbio_always_assert(check_eof_marker(handle, compressed_size));
		
		m_handle = std::move(handle);
		m_preferred_block_size = preferred_block_size;
		
		// Read the contents of the index.
		lb::file_istream index_stream(index_handle.get(), ios::never_close_handle);
		auto const entry_count(read_uint64(index_stream));
		m_index_entries.reserve(2 + entry_count); // Save space for the first and the last entry.
		m_index_entries.emplace_back(0, 0);
		for (std::uint64_t i(0); i < entry_count; ++i)
		{
			auto const compressed_offset(read_uint64(index_stream));
			auto const uncompressed_offset(read_uint64(index_stream));
			m_index_entries.emplace_back(compressed_offset, uncompressed_offset);
		}
		
		{
			// Sentinel
			auto const sentinel_compressed_offset(compressed_size - BGZIP_EOF_MARKER.size());
			auto const sentinel_uncompressed_offset(
				4 <= sentinel_compressed_offset
				? m_index_entries.back().uncompressed_offset + read_uncompressed_size(m_handle, sentinel_compressed_offset)
				: 0
			);
			m_index_entries.emplace_back(sentinel_compressed_offset, sentinel_uncompressed_offset);
		}

		// Sanity check.
		for (std::size_t i(1), count(m_index_entries.size()); i < count; ++i)
		{
			libbio_always_assert_lt(m_index_entries[i - 1].compressed_offset, m_index_entries[i].compressed_offset);
			libbio_always_assert_lt(m_index_entries[i - 1].uncompressed_offset, m_index_entries[i].uncompressed_offset);
		}
	}
	
	
	void bgzip_reader::read_blocks(std::size_t const count)
	{
		// Read the compressed block.
		libbio_assert_lt(count + m_current_block, m_index_entries.size());
		auto const offset(m_index_entries[m_current_block].compressed_offset);
		auto const next_offset(m_index_entries[count + m_current_block].compressed_offset);
		libbio_assert_lt(offset, next_offset);
		auto const length(next_offset - offset);
		m_input_buffer.resize(length);
		read_from_file(m_handle, offset, length, m_input_buffer.data());
		m_last_read_count = count;
	}
	
	
	std::size_t bgzip_reader::decompress(std::span <char> output_buffer) const
	{
		ios::filtering_streambuf <ios::input> in;
		in.push(ios::gzip_decompressor());
		in.push(ios::array_source(m_input_buffer.data(), m_input_buffer.size()));
		
		auto const retval(ios::read(in, output_buffer.data(), output_buffer.size()));
		// There should be no more compressed data available.
		// The filtering stream buffer is not seekable (but this seems to be the easiest way to 
		// decompress a gzip block), so we use the uncompressed size stored in the input.
		libbio_always_assert_eq(block_uncompressed_size(m_last_read_count), retval);
		
		return retval;
	}
	
	
	void check_matching_bgzip_index_entries(std::vector <bgzip_reader> const &readers)
	{
		// FIXME: at least the assumption that the indices have the same numbers of blocks does not seem to be valid. This might affect reverse_msa_reader. I could just replace it with msa_reader.
		// Check for matching index entries.
		auto const &first_handle(readers.front());
		auto const &first_entries(first_handle.index_entries());
		auto const first_count(first_handle.block_count());
		for (std::size_t i(1); i < readers.size(); ++i)
			libbio_always_assert_eq(readers[i].block_count(), first_count);
		
		for (std::size_t i(0); i < first_count; ++i)
		{
			for (std::size_t j(1); j < readers.size(); ++j)
			{
				auto const &entries(readers[j].index_entries());
				libbio_always_assert_eq(first_entries[i].uncompressed_offset, entries[i].uncompressed_offset);
			}
		}
	}
}
