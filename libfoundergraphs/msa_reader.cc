/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cstdio> // strerror
#include <founder_graphs/msa_reader.hh>
#include <founder_graphs/utility.hh>
#include <libbio/file_handling.hh>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/tail.hpp>
#include <range/v3/view/zip.hpp>

namespace fg	= founder_graphs;
namespace lb	= libbio;
namespace rsv	= ranges::views;


namespace {
	
	// Check overlap w.r.t. the first range.
	fg::range_overlap_type range_overlap(std::size_t const lb1, std::size_t const rb1, std::size_t const lb2, std::size_t const rb2)
	{
		if (lb1 <= lb2)
		{
			if (rb2 <= rb1)
				return fg::range_overlap_type::INCLUDES;
			
			if (lb2 < rb1)
				return fg::range_overlap_type::RIGHT_OVERLAP;
			
			return fg::range_overlap_type::DISJOINT;
		}
		else if (lb1 < rb2)
		{
			return fg::range_overlap_type::LEFT_OVERLAP;
		}
		return fg::range_overlap_type::DISJOINT;
	}
}


namespace founder_graphs {
	
	void text_msa_reader::add_file(std::string const &path)
	{
		bool const is_empty(m_handles.empty());
		auto const &handle(m_handles.emplace_back(lb::open_file_for_reading(path)));
		m_spans.emplace_back();
		
		auto const [aligned_size, preferred_block_size] = fg::check_file_size(handle);
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
	
	
	void text_msa_reader::prepare()
	{
		m_buffers.resize(m_handles.size());
		for (auto &buffer : m_buffers)
		{
			buffer.resize(m_preferred_block_size, 0);
			buffer.resize(0);
		}
	}
	
	
	bool text_msa_reader::fill_buffer(std::size_t const lb, std::size_t const rb, fill_buffer_callback_type &cb)
	{
		if (m_handles.empty())
			return false;
		
		auto const range_len(rb - lb);
		auto characters_left(range_len);
		auto buffer_size(m_buffers.front().size());
		std::size_t buffer_start_pos{};
		libbio_always_assert_lte(m_file_position - buffer_size, lb);
		
		// Seek if needed.
		// Also check if some of the buffer contents need to be saved.
		if (m_file_position < lb)
		{
			for (auto &handle : m_handles)
				handle.seek(lb);
		}
		else if (lb < m_file_position)
		{
			buffer_start_pos = m_file_position - lb;
			characters_left -= buffer_start_pos;
			if (m_file_position - buffer_size < lb)
			{
				auto const shift_amt(lb - (m_file_position - buffer_size));
				buffer_size -= shift_amt;
				for (auto &buffer : m_buffers)
				{
					std::move(buffer.begin() + shift_amt, buffer.end(), buffer.begin());
					buffer.resize(buffer_size);
				}
			}
		}
		
		if (lb + buffer_size < rb)
		{
			// Read a multiple of m_preferred_block_size.
			auto const read_amt(
				characters_left < m_preferred_block_size
				? m_preferred_block_size
				: characters_left - characters_left % m_preferred_block_size + m_preferred_block_size
			);
			
			// Handle the first items.
			auto &first_handle(m_handles.front());
			auto &first_buffer(m_buffers.front());
			first_buffer.resize(buffer_size + read_amt, 0);
			auto const actual_amt(first_handle.read(read_amt, first_buffer.data() + buffer_start_pos));
			// Handle the rest.
			for (auto &&[handle, buffer] : rsv::zip(rsv::tail(m_handles), rsv::tail(m_buffers)))
			{
				buffer.resize(buffer_size + read_amt, 0);
				auto const amt(handle.read(read_amt, buffer.data() + buffer_start_pos));
				libbio_always_assert_eq(actual_amt, amt);
			}
			
			m_file_position += read_amt;
		}
		
		for (auto &&[buffer, span] : rsv::zip(m_buffers, m_spans))
		{
			libbio_assert_lte(range_len, buffer.size());
			span = span_type(buffer.data(), range_len);
		}
		
		return cb(m_spans);
	}
	
	
	void bgzip_msa_reader::add_file(std::string const &path)
	{
		auto &handle(m_handles.emplace_back());
		handle.open(path);
	}
	
	
	void bgzip_msa_reader::prepare()
	{
		if (m_handles.empty())
			return;
		
		// Prepare the decompression group.
		m_decompress_group.reset(dispatch_group_create());
		
		// Initialize the other vectors.
		m_current_block_ranges.resize(m_handles.size());
		m_spans.resize(m_handles.size());
		m_buffers.resize(m_handles.size());
	}
	
	
	template <range_overlap_type t_overlap_type>
	void bgzip_msa_reader::update_decompressed(
		std::size_t const handle_idx,
		std::size_t const lb,
		std::size_t const rb,
		std::size_t const block_lb,
		std::size_t const block_rb
	)
	{
		static_assert(
			t_overlap_type == range_overlap_type::LEFT_OVERLAP	||
			t_overlap_type == range_overlap_type::RIGHT_OVERLAP	||
			t_overlap_type == range_overlap_type::DISJOINT
		);

		libbio_assert_lt(lb, rb);
		libbio_assert_lt(block_lb, block_rb);
		
		// We only write to the i-th elements of m_handles, m_current_block_ranges, m_spans, m_buffers
		// and make sure that no one else does the same, so no locking needed.
		auto &handle(m_handles[handle_idx]);
		auto &range(m_current_block_ranges[handle_idx]);
		auto &span(m_spans[handle_idx]);
		auto &buffer(m_buffers[handle_idx]);
		auto const &index_entries(handle.index_entries());
		
		auto const aln_lb(index_entries[block_lb].uncompressed_offset);
		auto const aln_rb(index_entries[block_rb].uncompressed_offset);
		auto const uncompressed_size(aln_rb - aln_lb);
		
		if constexpr (range_overlap_type::DISJOINT == t_overlap_type)
		{
			// Resize the buffer.
			buffer.resize(uncompressed_size);
			
			// Decompress.
			handle.decompress(std::span <char>(buffer.data(), buffer.size()));
		}
		else // LEFT_OVERLAP, RIGHT_OVERLAP
		{
			auto const current_aln_lb(index_entries[range.block_lb].uncompressed_offset);
			auto const current_aln_rb(index_entries[range.block_rb].uncompressed_offset);
			
			if constexpr (range_overlap_type::LEFT_OVERLAP == t_overlap_type)
			{
				// [block_lb, block_rb) is to the right from range.
				// Resize the buffer.
				buffer.resize(uncompressed_size);
				
				// Check if the buffer contents need to be shifted.
				libbio_assert_lte(aln_rb, current_aln_rb);
				auto const shift_amt(current_aln_rb - aln_rb);
				if (shift_amt)
					std::move_backward(buffer.begin(), buffer.begin() + shift_amt, buffer.end());
				
				// Decompress.
				handle.decompress(std::span <char>(buffer.data(), buffer.size() - shift_amt));
			}
			else  // RIGHT_OVERLAP
			{
				// [block_lb, block_rb) is to the left from range.
				// Check if the buffer contents need to be shifted.
				libbio_assert_lte(current_aln_lb, aln_lb);
				auto const shift_amt(aln_lb - current_aln_lb);
				if (shift_amt)
					std::move(buffer.begin() + shift_amt, buffer.end(), buffer.begin());
				
				// Decompress.
				auto const buffer_left_pad(current_aln_rb - aln_lb);
				buffer.resize(uncompressed_size);
				handle.decompress(std::span <char>(buffer.data() + buffer_left_pad, buffer.size() - buffer_left_pad));
			}
		}
		
		// Update the pointers.
		libbio_assert_lte(aln_lb, lb);
		libbio_assert_lte(rb, aln_rb);
		auto const span_left_pad(lb - aln_lb);
		auto const span_right_pad(aln_rb - rb);
		libbio_assert_lte(span_left_pad + span_right_pad, buffer.size());
		span = span_type(buffer.data() + span_left_pad, buffer.size() - span_right_pad - span_left_pad);
		range.block_lb = block_lb;
		range.block_rb = block_rb;
	}
	
	
	bool bgzip_msa_reader::fill_buffer(std::size_t const lb, std::size_t const rb, fill_buffer_callback_type &cb)
	{
		libbio_assert_lt(lb, rb);

		if (m_handles.empty())
			return false;

		libbio_assert_eq(m_handles.size(), m_current_block_ranges.size());
		libbio_assert_eq(m_handles.size(), m_spans.size());
		libbio_assert_eq(m_handles.size(), m_buffers.size());
		
		// Handle each bgzip_reader separately b.c. the block boundaries can be arbitrary.
		bool did_start_decompress{false};
		auto const queue(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
		for (auto &&[i, tup] : rsv::enumerate(rsv::zip(m_handles, m_current_block_ranges, m_spans, m_buffers)))
		{
			auto const i_(i); // For the ^{} blocks below; blocks don???t (at least as of Clang++12) copy local bindings.
			auto &[handle, current_range, span, buffer] = tup;
			auto const &index_entries(handle.index_entries());
			
			// Determine the blocks that contain the requested range.
			auto const new_range(handle.find_uncompressed_range(lb, rb));
			auto const block_lb(new_range.first); // Assign similarly to i_.
			auto const block_rb(new_range.second);

			try
			{
				libbio_assert_lte(rb, index_entries.back().uncompressed_offset); // Compare to the sentinel.
				libbio_assert_neq(block_lb, SIZE_MAX);
				libbio_assert_neq(block_rb, 0); // Take the addition into account.
				
				// Check if we need to shift the buffer contents.
				switch (range_overlap(current_range.block_lb, current_range.block_rb, block_lb, block_rb))
				{
					case range_overlap_type::INCLUDES:
					{
						// Just update the span.
						auto const current_offset(index_entries[current_range.block_lb].uncompressed_offset);
						libbio_assert_lte(current_offset, lb);
						span = span_type(buffer.data() + (lb - current_offset), rb - lb);
						break;
					}
					
					case range_overlap_type::LEFT_OVERLAP:
					{
						did_start_decompress = true;
						handle.block_seek(block_lb);
						handle.read_blocks(current_range.block_lb - block_lb);
						dispatch_group_async(*m_decompress_group, queue, ^{
							// Make sure everything can be copied or const-referenced, since we???re using a ^{} block.
							update_decompressed <range_overlap_type::LEFT_OVERLAP>(i_, lb, rb, block_lb, block_rb);
						});
						break;
					}
					
					case range_overlap_type::RIGHT_OVERLAP:
					{
						did_start_decompress = true;
						handle.block_seek(current_range.block_rb);
						handle.read_blocks(block_rb - current_range.block_rb);
						dispatch_group_async(*m_decompress_group, queue, ^{
							// Make sure everything can be copied or const-referenced, since we???re using a ^{} block.
							update_decompressed <range_overlap_type::RIGHT_OVERLAP>(i_, lb, rb, block_lb, block_rb);
						});
						break;
					}
					
					case range_overlap_type::DISJOINT:
					{
						did_start_decompress = true;
						handle.block_seek(block_lb);
						handle.read_blocks(block_rb - block_lb);
						dispatch_group_async(*m_decompress_group, queue, ^{
							// Make sure everything can be copied or const-referenced, since we???re using a ^{} block.
							update_decompressed <range_overlap_type::DISJOINT>(i_, lb, rb, block_lb, block_rb);
						});
					}
				}
			}
			catch (lb::assertion_failure_exception const &exc)
			{
				// For debugging.
				std::cerr << "lb: " << lb << " rb: " << rb << '\n';
				std::cerr << "i: " << i << '\n';
				std::cerr << "block_lb: " << block_lb << " block_rb: " << block_rb << '\n';
				std::cerr << "current_range: " << current_range << '\n';
				std::cerr << "handle.current_block_uncompressed_offset(): " << handle.current_block_uncompressed_offset() << '\n';
				std::cerr << "index_entries:\n";
				for (auto const &[i, entry] : rsv::enumerate(index_entries))
					std::cerr << i << ": " << entry << '\n';

				throw exc;
			}
		}
		
		if ((!did_start_decompress) || 0 == dispatch_group_wait(*m_decompress_group, DISPATCH_TIME_FOREVER))
			return cb(m_spans);
		else
			throw std::runtime_error("dispatch_group_wait failed even though DISPATCH_TIME_FOREVER was specified as timeout.");
	}
}
