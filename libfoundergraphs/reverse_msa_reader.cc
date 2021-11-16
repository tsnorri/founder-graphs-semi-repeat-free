/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cstdio> // strerror
#include <founder_graphs/reverse_msa_reader.hh>
#include <founder_graphs/utility.hh>
#include <libbio/assert.hh>
#include <libbio/dispatch/dispatch_fn.hh>
#include <libbio/file_handling.hh>
#include <range/v3/view/enumerate.hpp>
#include <tuple>

namespace lb	= libbio;
namespace rsv	= ranges::views;


namespace founder_graphs {
	
	void text_reverse_msa_reader::add_file(std::string const &path)
	{
		bool const is_empty(m_handles.empty());
		auto const &handle(m_handles.emplace_back(lb::open_file_for_reading(path)));
		
		auto const [aligned_size, preferred_block_size] = check_file_size(handle);
		if (is_empty)
		{
			m_aligned_size = aligned_size;
			m_preferred_block_size = preferred_block_size;
		}
		else
		{
			libbio_always_assert_eq(m_aligned_size, aligned_size);
		}
	}
	
	
	void text_reverse_msa_reader::prepare()
	{
		m_buffer.resize(m_handles.size() * m_preferred_block_size, 0);
		m_file_position = m_aligned_size;
	}
	
	
	bool text_reverse_msa_reader::fill_buffer(fill_buffer_callback_type &cb)
	{
		if (0 == m_file_position)
		{
			cb(false);
			return false;
		}
		
		m_current_block_size = std::min(m_file_position, m_preferred_block_size);
		m_file_position -= m_current_block_size;
		for (auto const &[i, handle] : rsv::enumerate(m_handles))
			read_from_file(handle, m_file_position, m_current_block_size, m_buffer.data() + i * m_current_block_size);
		
		return cb(true);
	}
	
	
	void bgzip_reverse_msa_reader::add_file(std::string const &path)
	{
		auto &handle(m_handles.emplace_back());
		handle.open(path);
	}
	
	
	void bgzip_reverse_msa_reader::prepare()
	{
		if (m_handles.empty())
			return;
		
		// Prepare the decompression group.
		m_decompress_group.reset(dispatch_group_create());
		
		// Check the index entries.
		check_matching_bgzip_index_entries(m_handles);
		
		// Determine the max. block size.
		auto const &first_handle(m_handles.front());
		auto const &first_entries(first_handle.index_entries());
		auto const first_count(first_handle.block_count());
		std::size_t max_uncompressed_block_size{};
		for (std::size_t i(1); i < first_count; ++i)
		{
			auto const prev_offset(first_entries[i - 1].uncompressed_offset);
			auto const offset(first_entries[i].uncompressed_offset);
			max_uncompressed_block_size = std::max(max_uncompressed_block_size, offset - prev_offset);
		}
		
		// Reserve memory.
		m_buffer.resize(m_handles.size() * max_uncompressed_block_size, 0);
		
		// Move past the last block.
		for (std::size_t i(0); i < m_handles.size(); ++i)
		{
			auto &handle(m_handles[i]);
			handle.block_seek(first_count);
		}
	}
	
	
	bool bgzip_reverse_msa_reader::fill_buffer(fill_buffer_callback_type &cb)
	{
		if (0 == m_handles.front().current_block())
		{
			cb(false);
			return false;
		}
		
		// Seek.
		for (auto &handle : m_handles)
		{
			bool const status(handle.block_seek_previous());
			libbio_assert(status);
		}
		
		// Update the uncompressed size.
		auto const &first_handle(m_handles.front());
		m_current_block_size = first_handle.current_block_uncompressed_size();
		m_buffer.resize(m_handles.size() * m_current_block_size);
		
		// Fill the buffers and decompress.
		{
			auto const queue(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
			for (std::size_t i(0); i < m_handles.size(); ++i)
			{
				auto &handle(m_handles[i]);
				handle.read_current_block();
				lb::dispatch_group_async_fn(*m_decompress_group, queue, [this, i, &handle](){
					auto const it(m_buffer.data() + i * m_current_block_size);
					auto const end(it + m_current_block_size);
					handle.decompress(std::span(it, end));
				});
			}
			
			if (0 == dispatch_group_wait(*m_decompress_group, DISPATCH_TIME_FOREVER))
				return cb(true);
			else
				throw std::runtime_error("dispatch_group_wait failed even though DISPATCH_TIME_FOREVER was specified as timeout.");
		}
	}
}
