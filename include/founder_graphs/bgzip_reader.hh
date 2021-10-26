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
	
	typedef std::vector <bgzip_index_entry> index_entry_vector;
	
	
	class bgzip_reader
	{
	protected:
		libbio::file_handle	m_handle;
		index_entry_vector	m_index_entries;
		std::vector <char>	m_input_buffer;
		std::size_t			m_current_block{};
		std::size_t			m_preferred_block_size{};
		
	public:
		void open(std::string const &path);
		void open(libbio::file_handle &&handle, libbio::file_handle &index_handle);
		
		index_entry_vector const &index_entries() const { return m_index_entries; }
		
		std::size_t current_block() const { return m_current_block; }
		std::size_t block_count() const { return m_index_entries.size() - 1; }
		void block_seek(std::size_t const block) { m_current_block = block; } // No bounds check.
		inline bool block_seek_previous();
		inline bool block_seek_next();
		inline std::size_t uncompressed_size() const;
		inline std::size_t current_block_uncompressed_offset() const;
		inline std::size_t current_block_uncompressed_size() const;
		void read_current_block();
		std::size_t decompress(std::span <char> buffer) const;
	};
	
	
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
	
	
	std::size_t bgzip_reader::current_block_uncompressed_offset() const
	{
		return m_index_entries[m_current_block].uncompressed_offset;
	}
	
	
	std::size_t bgzip_reader::current_block_uncompressed_size() const
	{
		libbio_assert_lt(1 + m_current_block, m_index_entries.size());
		return m_index_entries[1 + m_current_block].uncompressed_offset - current_block_uncompressed_offset();
	}
}

#endif
