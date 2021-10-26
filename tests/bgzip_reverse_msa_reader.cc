/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <algorithm>
#include <boost/format.hpp>
#include <catch2/catch.hpp>
#include <founder_graphs/reverse_msa_reader.hh>
#include <libbio/file_handling.hh>

namespace fg	= founder_graphs;
namespace gen	= Catch::Generators;
namespace lb	= libbio;


namespace {

	std::size_t compare_blocks(fg::bgzip_reader &bgzip_reader, lb::file_istream &expected_stream, std::vector <char> &buffer_1, std::vector <char> &buffer_2)
	{
		bgzip_reader.read_current_block();
		
		// Decompress.
		auto const offset(bgzip_reader.current_block_uncompressed_offset());
		auto const size(bgzip_reader.current_block_uncompressed_size());
		buffer_1.resize(size);
		bgzip_reader.decompress(std::span(buffer_1.data(), buffer_1.data() + size));
		
		// Read the expected data.
		buffer_2.resize(size);
		expected_stream.seekg(offset);
		expected_stream.read(buffer_2.data(), size);
		REQUIRE(std::equal(buffer_1.begin(), buffer_1.end(), buffer_2.begin(), buffer_2.end()));
		
		return size;
	}
}


SCENARIO("bgzip_reverse_msa_reader can read a set of files")
{
	GIVEN("a set of bgzip files")
	{
		// Uncompressed files
		constexpr std::size_t input_count{4};
		REQUIRE(0 < input_count);
		std::array <std::vector <char>, input_count> expected_data;
		{
			boost::format fmt("test-files/equal-length-1/%d");
			for (std::size_t i(0); i < input_count; ++i)
			{
				lb::file_istream input;
				auto const fname(boost::str(fmt % (1 + i)));
				lb::open_file_for_reading(fname, input);
				
				// Allocate memory if the size is known.
				// (I could also check it using stat().)
				if (i)
					expected_data[i].reserve(expected_data[i - 1].size());
				
				// Copy the contents to the buffer.
				std::copy(
					std::istream_iterator <char>(input),
					std::istream_iterator <char>(),
					std::back_inserter(expected_data[i])
				);
				
				// Make sure the sizes match.
				if (i)
					REQUIRE(expected_data[i].size() == expected_data[i - 1].size());
			}
			REQUIRE(0 < expected_data.front().size());
		}
		
		// Compressed files
		fg::bgzip_reverse_msa_reader msa_reader;
		{
			boost::format fmt("test-files/equal-length-1/%d.gz");
			for (std::size_t i(0); i < input_count; ++i)
			{
				auto const fname(boost::str(fmt % (1 + i)));
				msa_reader.add_file(fname);
			}
			
			msa_reader.prepare();
		}
		
		WHEN("the files are read")
		{
			THEN("the contents match the originals")
			{
				std::size_t base_position{};
				std::size_t handled_characters{};
				std::size_t const input_size(expected_data.front().size());
				REQUIRE(msa_reader.aligned_size() == input_size);
				while (msa_reader.fill_buffer(
					[
						input_count,
						&base_position,
						&msa_reader,
						&handled_characters,
						input_size,
						&expected_data
					](bool const did_read){
						if (!did_read)
							return false;
						
						auto const block_size(msa_reader.block_size());
						auto const total_block_size(input_count * block_size);
						auto const &buffer(msa_reader.buffer());
						REQUIRE(buffer.size() == total_block_size);
						
						for (std::size_t i(0); i < input_count; ++i)
						{
							auto const &expected_seq(expected_data[i]);
							for (std::size_t j(0); j < block_size; ++j)
							{
								auto const buffer_pos((i + 1) * block_size - j - 1);
								auto const expected_seq_pos(input_size - (base_position + j) - 1);
								REQUIRE(buffer[buffer_pos] == expected_seq[expected_seq_pos]);
								++handled_characters;
							}
						}
						
						base_position += block_size;
						return true;
					}
				));
				
				REQUIRE(handled_characters == input_count * msa_reader.aligned_size());
			}
		}
	}
}
