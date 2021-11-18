/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_BGZIP_READER_HH
#define FOUNDER_GRAPHS_BGZIP_READER_HH

#include <cstddef>
#include <libbio/assert.hh>
#include <libbio/cxxcompat.hh>
#include <libbio/file_handle.hh>
#include <ostream>
#include <string>
#include <vector>


namespace founder_graphs {
	
	struct bgzip_index_entry
	{
		std::size_t	compressed_offset{};
		std::size_t	uncompressed_offset{};
		
		bgzip_index_entry() = default;
		
		bgzip_index_entry(std::size_t const compressed_offset_, std::size_t const uncompressed_offset_):
			compressed_offset(compressed_offset_),
			uncompressed_offset(uncompressed_offset_)
		{
		}
		
		bool operator<(bgzip_index_entry const &other) const { return compressed_offset < other.compressed_offset; }
	};

	inline std::ostream &operator<<(std::ostream &os, bgzip_index_entry const &entry)
	{
		os << "compressed_offset: " << entry.compressed_offset << " uncompressed_offset: " << entry.uncompressed_offset;
		return os;
	}
	
	struct bgzip_index_entry_uncompressed_offset_cmp
	{
		bool operator()(bgzip_index_entry const &lhs, std::size_t const rhs) const { return lhs.uncompressed_offset < rhs; }
		bool operator()(std::size_t const lhs, bgzip_index_entry const &rhs) const { return lhs < rhs.uncompressed_offset; }
	};
	
	typedef std::vector <bgzip_index_entry> index_entry_vector;
	
	
	class bgzip_reader
	{
	protected:
		libbio::file_handle	m_handle;
		index_entry_vector	m_index_entries;
		std::vector <char>	m_input_buffer;
		std::size_t			m_current_block{};
		std::size_t			m_preferred_block_size{};
		std::size_t			m_last_read_count{};
		
	public:
		void open(std::string const &path);
		void open(libbio::file_handle &&handle, libbio::file_handle &index_handle);
		
		index_entry_vector const &index_entries() const { return m_index_entries; }
		
		inline std::size_t find_uncompressed_offset_lb(std::size_t const offset) const;
		std::size_t find_uncompressed_offset_rb(std::size_t const offset) const { return find_uncompressed_offset_rb(offset, 0); }
		inline std::size_t find_uncompressed_offset_rb(std::size_t const offset, std::size_t const start) const;
		inline std::pair <std::size_t, std::size_t> find_uncompressed_range(std::size_t const lb, std::size_t const rb) const;
		
		std::size_t current_block() const { return m_current_block; }
		std::size_t block_count() const { return m_index_entries.size() - 1; }
		void block_seek(std::size_t const block) { m_current_block = block; } // No bounds check.
		inline bool block_seek_previous();
		inline bool block_seek_next();
		inline std::size_t uncompressed_size() const;
		inline std::size_t current_block_compressed_offset() const;
		inline std::size_t current_block_uncompressed_offset() const;
		std::size_t current_block_compressed_size() const { return block_compressed_size(1); }
		std::size_t current_block_uncompressed_size() const { return block_uncompressed_size(1); }
		inline std::size_t block_compressed_size(std::size_t const count) const; // Relative to current position.
		inline std::size_t block_uncompressed_size(std::size_t const count) const; // Relative to current position.
		void read_current_block() { read_blocks(1); }
		void read_blocks(std::size_t const count);
		std::size_t decompress(std::span <char> buffer) const;
	};
	
	
	std::size_t bgzip_reader::find_uncompressed_offset_lb(std::size_t const offset) const
	{
		auto const it(std::upper_bound(m_index_entries.begin(), m_index_entries.end(), offset, bgzip_index_entry_uncompressed_offset_cmp()));
		if (m_index_entries.end() == it)
			return SIZE_MAX;
		return it - m_index_entries.begin() - 1;
	}



	std::size_t bgzip_reader::find_uncompressed_offset_rb(std::size_t const offset, std::size_t const start) const
	{
		auto const it(std::lower_bound(m_index_entries.begin() + start, m_index_entries.end(), offset, bgzip_index_entry_uncompressed_offset_cmp()));
		if (m_index_entries.end() == it)
			return SIZE_MAX;
		return it - m_index_entries.begin();
	}
	

	std::pair <std::size_t, std::size_t> bgzip_reader::find_uncompressed_range(std::size_t const lb, std::size_t const rb) const
	{
		auto const block_lb(find_uncompressed_offset_lb(lb));
		if (SIZE_MAX == block_lb)
			return {SIZE_MAX, SIZE_MAX};
		auto const block_rb(find_uncompressed_offset_rb(rb, block_lb));
		return {block_lb, block_rb};
	}
	
	
	bool bgzip_reader::block_seek_previous()
	{
		if (m_current_block)
		{
			--m_current_block;
			return true;
		}
		return false;
	}
	
	
	bool bgzip_reader::block_seek_next()
	{
		auto const next(1 + m_current_block);
		if (next < block_count())
		{
			m_current_block = next;
			return true;
		}
		return false;
	}
			
			
	std::size_t bgzip_reader::uncompressed_size() const
	{
		return m_index_entries.back().uncompressed_offset;
	}
	
	
	std::size_t bgzip_reader::current_block_compressed_offset() const
	{
		return m_index_entries[m_current_block].uncompressed_offset;
	}
	
	
	std::size_t bgzip_reader::current_block_uncompressed_offset() const
	{
		return m_index_entries[m_current_block].uncompressed_offset;
	}
	
	
	std::size_t bgzip_reader::block_compressed_size(std::size_t const count) const
	{
		libbio_always_assert_lt(count + m_current_block, m_index_entries.size());
		return m_index_entries[count + m_current_block].compressed_offset - current_block_uncompressed_offset();
	}
	
	
	std::size_t bgzip_reader::block_uncompressed_size(std::size_t const count) const
	{
		libbio_always_assert_lt(count + m_current_block, m_index_entries.size());
		return m_index_entries[count + m_current_block].uncompressed_offset - current_block_uncompressed_offset();
	}
	
	
	// For checking that MSAs have matching indices.
	void check_matching_bgzip_index_entries(std::vector <bgzip_reader> const &readers);
}

#endif
