/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cereal/types/string.hpp>
#include <founder_graphs/basic_types.hh>
#include <founder_graphs/founder_graph_index.hh>
#include <libbio/dispatch.hh>
#include <libbio/file_handling.hh>
#include <libbio/int_vector.hh>
#include <range/v3/view/enumerate.hpp>
//#include <syncstream>

namespace fg	= founder_graphs;
namespace lb	= libbio;
namespace rsv	= ranges::views;


namespace {
	
	void copy_words(lb::atomic_bit_vector const &src_vec, sdsl::bit_vector &dst_vec)
	{
		auto *dst(dst_vec.data());
		static_assert(std::is_same_v <std::remove_pointer_t <decltype(dst)>, lb::atomic_bit_vector::word_type>);
		libbio_assert_eq((dst_vec.size() - 1) / 64 + 1, src_vec.word_size());
		std::copy(src_vec.word_begin(), src_vec.word_end(), dst);
	}
	
	
	class dispatch_semaphore_guard
	{
	protected:
		dispatch_semaphore_t	m_semaphore{};
		
	public:
		dispatch_semaphore_guard(dispatch_semaphore_t semaphore):
			m_semaphore(semaphore)
		{
		}
		
		dispatch_semaphore_guard(dispatch_semaphore_guard const &) = delete;
		
		dispatch_semaphore_guard &operator=(dispatch_semaphore_guard const &) = delete;
		
		~dispatch_semaphore_guard()
		{
			libbio_assert(m_semaphore);
			dispatch_semaphore_signal(m_semaphore);
		}
	};
	
	
	void construct_csa(
		fg::founder_graph_indices::csa_type &csa,
		std::string const &text_path,
		char const *sa_path,
		char const *bwt_path,
		std::string const &block_content_path,
		bool const text_is_zero_terminated
	)
	{
		sdsl::cache_config config(false); // Do not remove temporary files automatically.
		
		if (sa_path)
			config.file_map[sdsl::conf::KEY_SA] = sa_path;
		
		if (bwt_path)
			config.file_map[sdsl::conf::KEY_BWT] = bwt_path;
		
		if (text_is_zero_terminated)
		{
			config.file_map[sdsl::conf::KEY_TEXT] = text_path;
			
			if (!sa_path)
			{
				// The suffix array needs to be constructed b.c. sa_path was not set.
				// SDSL’s construct() expects the text to have the integer vector header, but we don’t output that.
				// Hence we construct the suffix array here.
				sdsl::read_only_mapper <8> text(text_path, true, false);
				auto const fname(sdsl::cache_file_name(sdsl::conf::KEY_SA, config));
				auto suffix_array(
					sdsl::write_out_mapper <0>::create(
						fname,
						0,
						sdsl::bits::hi(text.size()) + 1
					)
				);
				sdsl::algorithm::calculate_sa(reinterpret_cast <unsigned char const *>(text.data()), text.size(), suffix_array);
				config.file_map[sdsl::conf::KEY_SA] = fname;
				sa_path = config.file_map[sdsl::conf::KEY_SA].data(); // std::string is guaranteed to be zero-terminated.
			}
			
			if (!bwt_path)
			{
				// Same as with the suffix array.
				sdsl::read_only_mapper <8> text(text_path, true, false);
				auto const text_size(text.size());
				
				std::size_t const buffer_size(1000000U); // Same as in SDSL, multiple of 8.
				sdsl::int_vector_buffer <> sa_buf(sa_path, std::ios::in, buffer_size);
				
				auto const fname(sdsl::cache_file_name(sdsl::conf::KEY_BWT, config));
				auto bwt(sdsl::write_out_mapper <8>::create(fname, text_size, 8));
				
				for (std::size_t i(0); i < text_size; ++i)
				{
					auto const pos(sa_buf[i]);
					auto const idx(pos ? pos - 1 : text.size() - 1);
					bwt[i] = text[idx];
					//std::cerr << i << '\t' << pos << '\t' << text[idx] << '\t' << int(text[idx]) << '\n';
				}
				
				config.file_map[sdsl::conf::KEY_BWT] = fname;
				bwt_path = config.file_map[sdsl::conf::KEY_BWT].data();
			}
		}
		
		// SDSL’s construct() does not use the given text if KEY_TEXT is set in config.
		sdsl::construct(csa, text_path, config, 1);
	}
}


namespace founder_graphs {
	
	bool founder_graph_index::construct(
		std::string const &text_path,
		char const *sa_path,
		char const *bwt_path,
		std::string const &block_content_path,
		bool const text_is_zero_terminated,
		founder_graph_index_construction_delegate &delegate
	)
	{
		// Construct the founder graph index. We first use SDSL to construct the BWT index.
		// Then we read the block contents (i.e. segments), try to locate them in the index
		// and finally store the lexicographic range boundaries in m_b_positions and m_e_positions.
		// Since each segment can be searched separately, we do this in parallel and write
		// the boundaries to atomic bit vectors.
		
		// The block contents file contains one or more records as follows:
		// fg::length_type			Number of blocks in the file.
		// Blocks as follows:
		//	fg::length_type			Number of segments in the current block.
		//	Segments as follows:
		//		fg::length_type		Number of segments (in the same block) the prefix of which this segment is.
		//		fg::length_type		Number of edges that have the segment as an endpoint.
		//		std::string			Segment
		
		lb::file_istream block_content_stream;
		lb::open_file_for_reading(block_content_path, block_content_stream);
		cereal::PortableBinaryInputArchive archive(block_content_stream);
		
		// Build the CSA.
		construct_csa(m_csa, text_path, sa_path, bwt_path, block_content_path, text_is_zero_terminated);
		
		lb::dispatch_ptr <dispatch_group_t> group_ptr(dispatch_group_create());
		lb::dispatch_ptr <dispatch_semaphore_t> sema_ptr(dispatch_semaphore_create(256)); // Limit the number of segments read in the current thread.
		auto group(*group_ptr);
		auto sema(*sema_ptr);
		auto concurrent_queue(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
		
		// Read the segments.
		{
			std::atomic_bool status{true};
			
			// Since the positions in B and E are expected to be at least somewhat far apart and
			// setting the values does not require specific order, just consistency, we use
			// atomic bit vectors instead of a serial callback queue. This should be (mostly) fine b.c.
			// cache line size on x86-64 is 64 bytes and on M1 128 bytes.
			lb::atomic_bit_vector b_positions(m_csa.size());
			lb::atomic_bit_vector e_positions(m_csa.size());
			
			// If I read the reference correctly, the vectors should be zero-filled in C++20, but make sure anyway.
			for (auto &word : b_positions.word_range()) word.store(0, std::memory_order_relaxed);
			for (auto &word : e_positions.word_range()) word.store(0, std::memory_order_relaxed);
			
			// Buffers for the current block.
			std::vector <std::string> segments;
			std::vector <fg::length_type> prefix_counts;
			std::vector <fg::length_type> edge_count_csum;
			
			{
				std::size_t seg_idx{};
				fg::length_type block_count{};
				archive(cereal::make_size_tag(block_count));
				for (fg::length_type i(0); i < block_count; ++i)
				{
					// Read the segment count.
					fg::length_type segment_count{};
					archive(cereal::make_size_tag(segment_count));
					
					// Resize the buffers.
					segments.clear();
					prefix_counts.clear();
					edge_count_csum.clear();
					segments.resize(segment_count);
					prefix_counts.resize(segment_count, 0);
					edge_count_csum.resize(1 + segment_count, 0);
					
					// Read the current block and calculate the cumulative sum of edge counts.
					for (fg::length_type j(0); j < segment_count; ++j)
					{
						archive(prefix_counts[j]);
						archive(edge_count_csum[1 + j]);
						archive(segments[j]);
						edge_count_csum[1 + j] += edge_count_csum[j];
					}
					
					// Determine the lexicographic range in a worker thread.
					for (fg::length_type j(0); j < segment_count; ++j)
					{
						dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
						
						auto const prefix_count(prefix_counts[j]);
						auto const expected_occurrence_count(edge_count_csum[prefix_count + 1 + j] - edge_count_csum[j]);
						auto &segment(segments[j]);
						
						lb::dispatch_group_async_fn(
							group,
							concurrent_queue,
							[
								this,
								sema,
								block_idx = i,
								seg_idx,
								segment = std::move(segment),
								expected_occurrence_count,
								&b_positions,
								&e_positions,
								&status,
								&delegate
							](){
								dispatch_semaphore_guard guard(sema);
								
								size_type ll{};
								size_type rr{m_csa.size() - 1};
								size_type res{};
								for (auto const [i, cc] : rsv::enumerate(segment))
								{
									// The CSA is constructed from the reverse of the text, so we can forward-search using backward_search.
									res = sdsl::backward_search(m_csa, ll, rr, cc, ll, rr);
						
									if (0 == res)
									{
										delegate.zero_occurrences_for_segment(block_idx, seg_idx, segment, cc, i);
										status.store(false, std::memory_order_relaxed);
										return;
									}
								}
								
								if (expected_occurrence_count != res)
								{
									delegate.unexpected_number_of_occurrences_for_segment(block_idx, seg_idx, segment, expected_occurrence_count, res);
									status.store(false, std::memory_order_relaxed);
									return;
								}
								
								// Set the values.
								{
									auto const b_res(b_positions.fetch_or(ll, 0x1, std::memory_order_relaxed));
									auto const e_res(e_positions.fetch_or(rr, 0x1, std::memory_order_relaxed));
									
									if (b_res)
									{
										delegate.position_in_b_already_set(ll);
										status.store(false, std::memory_order_relaxed);
									}
									
									if (e_res)
									{
										delegate.position_in_b_already_set(rr);
										status.store(false, std::memory_order_relaxed);
									}
								}
							}
						);
						
						if (!status.load(std::memory_order_acquire))
							goto exit_loop;
						
						++seg_idx;
					}
				}
				
			exit_loop:
				// Wait until the tasks are finished before releasing anything.
				// (I think the blocks retain the associated queue, though, so we might be able to just return.
				// The semaphore does need to get incremented to its old value before that.)
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
