/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <founder_graphs/founder_graph_index.hh>
#include <libbio/dispatch.hh>
#include <libbio/file_handling.hh>
#include <libbio/int_vector.hh>
#include <range/v3/view/enumerate.hpp>
//#include <syncstream>

namespace lb	= libbio;
namespace rsv	= ranges::views;


namespace {
	
	std::ostream &synchronize_ostream(std::ostream &stream)
	{
		// My libc++ doesn’t yet have std::osyncstream.
		return stream;
	}
	
	
	void copy_words(lb::atomic_bit_vector const &src_vec, sdsl::bit_vector &dst_vec)
	{
		auto *dst(dst_vec.data());
		static_assert(std::is_same_v <std::remove_pointer_t <decltype(dst)>, lb::atomic_bit_vector::word_type>);
		libbio_assert_eq(dst_vec.size() / 64, src_vec.word_size());
		std::copy(src_vec.word_begin(), src_vec.word_end(), dst);
	}
}


namespace founder_graphs {
	
	bool founder_graph_index::construct(std::string const &text_path, std::string const &block_content_path, std::ostream &error_os)
	{
		// Construct the founder graph index. We first use SDSL to construct the BWT index.
		// Then we read the block contents (i.e. segments), try to locate them in the index
		// and finally store the lexicographic range boundaries in m_b_positions and m_e_positions.
		// Since each segment can be searched separately, we do this in parallel and write
		// the boundaries to atomic bit vectors.
		
		lb::file_istream block_content_stream;
		lb::open_file_for_reading(block_content_path, block_content_stream);
		cereal::PortableBinaryInputArchive archive(block_content_stream);
		
		// Build the CSA.
		sdsl::construct(m_csa, text_path, 1);
		
		lb::dispatch_ptr <dispatch_group_t> group_ptr(dispatch_group_create());
		auto group(*group_ptr);
		auto concurrent_queue(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
		
		// Read the segments.
		{
			std::atomic_bool status{true};
			
			size_type size{};
			archive(cereal::make_size_tag(size));
			
			// Since the positions in B and E are expected to be at least somewhat far apart and
			// setting the values does not require specific order, just consistency, we use
			// atomic bit vectors instead of a serial callback queue. This should be fine b.c.
			// cache line size on x86-64 is 64 bytes and on M1 128 bytes.
			lb::atomic_bit_vector b_positions(size);
			lb::atomic_bit_vector e_positions(size);
			
			// If I read the reference correctly, the vectors should be zero-filled in C++20, but make sure anyway.
			for (auto &word : b_positions.word_range()) word.store(0, std::memory_order_relaxed);
			for (auto &word : e_positions.word_range()) word.store(0, std::memory_order_relaxed);
			
			{
				for (size_type i(0); i < size; ++i)
				{
					std::string segment;
					size_type prefix_count{};
					archive(segment);
					archive(prefix_count);
					
					lb::dispatch_group_async_fn(
						group,
						concurrent_queue,
						[
							this,
							seg_idx = i,
							segment = std::move(segment),
							prefix_count,
							&b_positions,
							&e_positions,
							&status,
							&error_os
						](){
							size_type ll{};
							size_type rr{m_csa.size()};
							size_type res{};
							for (auto const [i, cc] : rsv::enumerate(segment))
							{
								// The CSA is constructed from the reverse of the text, so we can forward-search using backward_search.
								res = sdsl::backward_search(m_csa, ll, rr, cc, ll, rr);
						
								if (0 == res)
								{
									synchronize_ostream(error_os) << "ERROR: got zero occurrences when searching for ‘" << cc << "’ at index " << i << " of segment " << seg_idx << ": “" << segment << "”.\n";
									status.store(false, std::memory_order_relaxed);
									return;
								}
							}
					
							if (1 + prefix_count != res)
							{
								synchronize_ostream(error_os) << "ERROR: got " << res << " occurrences while " << (1 + prefix_count) << " were expected when searching for segment " << seg_idx << ": “" << segment << "”.\n";
								status.store(false, std::memory_order_relaxed);
								return;
							}
						
							// Set the values.
							{
								auto const b_res(b_positions.fetch_or(ll, 0x1, std::memory_order_relaxed));
								auto const e_res(e_positions.fetch_or(rr, 0x1, std::memory_order_relaxed));
							
								if (b_res)
								{
									synchronize_ostream(error_os) << "ERROR: position " << ll << " in B already set.\n";
									status.store(false, std::memory_order_relaxed);
								}
							
								if (e_res)
								{
									synchronize_ostream(error_os) << "ERROR: position " << rr << " in E already set.\n";
									status.store(false, std::memory_order_relaxed);
								}
							}
						}
					);
					
					if (status.load(std::memory_order_acquire))
						break;
				}
				
				// Wait until the tasks are finished before releasing anything.
				// (I think the blocks retain the associated queue, though, so we might be able to just return.)
				dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
				
				if (!status)
					return false;
				
				// Copy the values.
				{
					auto const size(m_csa.size());
					
					m_b_positions.resize(size, 0);
					m_e_positions.resize(size, 0);
					
					copy_words(b_positions, m_b_positions);
					copy_words(e_positions, m_e_positions);
				}
			}
		}
		
		// Prepare rank and select support.
		{
			dispatch_group_async(group, concurrent_queue, ^{
				sdsl::util::init_support(m_b_positions_rank1_support, &m_b_positions);
			});
			dispatch_group_async(group, concurrent_queue, ^{
				sdsl::util::init_support(m_b_positions_select1_support, &m_b_positions);
			});
			dispatch_group_async(group, concurrent_queue, ^{
				sdsl::util::init_support(m_e_positions_select1_support, &m_e_positions);
			});
			dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
		}
		
		return true;
	}
}
