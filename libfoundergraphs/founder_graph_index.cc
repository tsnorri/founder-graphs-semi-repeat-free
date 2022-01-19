/*
 * Copyright (c) 2021-2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cereal/types/string.hpp>
#include <founder_graphs/basic_types.hh>
#include <founder_graphs/founder_graph_index.hh>
#include <libbio/assert.hh>
#include <libbio/bits.hh>
#include <libbio/dispatch.hh>
#include <libbio/file_handling.hh>
#include <libbio/int_vector.hh>
#include <range/v3/view/enumerate.hpp>
//#include <syncstream>

namespace fg	= founder_graphs;
namespace fgi	= founder_graphs::founder_graph_indices;
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
	
	
	struct interval_symbols_context
	{
		typedef fgi::csa_type			csa_type;
		typedef csa_type::size_type		size_type;
		typedef csa_type::value_type	value_type;
		
		std::vector <value_type>		cs;
		std::vector <size_type>			rank_c_i;
		std::vector <size_type>			rank_c_j;
		
		interval_symbols_context() = default;
		
		interval_symbols_context(csa_type const &csa):
			cs(csa.sigma, 0),
			rank_c_i(csa.sigma, 0),
			rank_c_j(csa.sigma, 0)
		{
		}
	};
	
	
	struct lexicographic_range
	{
		typedef fgi::csa_type		csa_type;
		typedef csa_type::size_type	size_type;
		typedef csa_type::char_type	char_type;
		
		size_type lb{};
		size_type rb{};
		
		lexicographic_range() = default;
		
		lexicographic_range(csa_type const &csa):
			rb(csa.size() - 1)
		{
		}
		
		size_type size() const { return rb - lb + 1; }
		
		size_type backward_search(csa_type const &csa, char_type const cc)
		{
			return sdsl::backward_search(csa, lb, rb, cc, lb, rb);
		}
		
		size_type interval_symbols(csa_type const &csa, interval_symbols_context &ctx) const
		{
			size_type retval{};
			// interval_symbols takes a half-open range.
			csa.wavelet_tree.interval_symbols(lb, 1 + rb, retval, ctx.cs, ctx.rank_c_i, ctx.rank_c_j);
			return retval;
		}
	};
	
	
	struct lexicographic_range_pair
	{
		typedef fgi::csa_type		csa_type;
		typedef csa_type::size_type	size_type;
		typedef csa_type::char_type	char_type;
		
		lexicographic_range	range{};
		lexicographic_range co_range{};
		
		lexicographic_range_pair() = default;
		
		lexicographic_range_pair(csa_type const &csa):
			range(csa),
			co_range(range)
		{
		}
		
		// Maintain both ranges.
		size_type backward_search(csa_type const &csa, char_type const cc)
		{
			libbio_assert_neq(cc, 0);
			auto const [kk, vec] = csa.wavelet_tree.range_search_2d(range.lb, range.rb, 0, cc - 1, false);
			auto const retval(range.backward_search(csa, cc));
			co_range.lb += kk;
			co_range.rb = co_range.lb + retval;
			return retval;
		}
	};
	
	
	struct segment_length_description
	{
		typedef fgi::csa_type::size_type	size_type;
		typedef fg::length_type				length_type;
		
		size_type		lexicographic_rank{};
		std::size_t		segment_length{};
		length_type		block_index{};
		
		segment_length_description() = default;
		
		segment_length_description(
			size_type lexicographic_rank_,
			std::size_t	segment_length_,
			length_type	block_index_
		):
			lexicographic_rank(lexicographic_rank_),
			segment_length(segment_length_),
			block_index(block_index_)
		{
		}
	};
	
	
	void construct_csa(
		fgi::csa_type &csa,
		std::string const &text_path,
		char const *sa_path,
		char const *bwt_path,
		bool const text_is_zero_terminated
	)
	{
		// FIXME: remove logging or use delegate.

		sdsl::cache_config config(false); // Do not remove temporary files automatically.
		
		if (sa_path)
		{
			lb::log_time(std::cerr) << "Using suffix array at " << sa_path << ".\n"; 
			config.file_map[sdsl::conf::KEY_SA] = sa_path;
		}
		
		if (bwt_path)
		{
			lb::log_time(std::cerr) << "Using BWT at " << bwt_path << ".\n"; 
			config.file_map[sdsl::conf::KEY_BWT] = bwt_path;
		}
		
		if (text_is_zero_terminated)
		{
			lb::log_time(std::cerr) << "Using zero-terminated text at " << text_path << ".\n"; 
			config.file_map[sdsl::conf::KEY_TEXT] = text_path;
			
			if (!sa_path)
			{
				// The suffix array needs to be constructed b.c. sa_path was not set.
				// SDSL’s construct() expects the text to have the integer vector header, but we don’t output that.
				// Hence we construct the suffix array here.
				lb::log_time(std::cerr) << "Building suffix array…\n"; 
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
				// FIXME: The BWT transform below produces incorrect results under some conditions.
				// Same as with the suffix array.
				lb::log_time(std::cerr) << "Building BWT…\n"; 
				sdsl::read_only_mapper <8> text(text_path, true, false);
				auto const text_size(text.size());
				
				std::size_t const buffer_size(1024U * 1024U * 512U); // Same as in SDSL, multiple of 8.
				sdsl::int_vector_buffer <> sa_buf(sa_path, std::ios::in, buffer_size);
				
				auto const fname(sdsl::cache_file_name(sdsl::conf::KEY_BWT, config));
				lb::log_time(std::cerr) << "Writing to " << fname << '\n';
				std::vector <char> bwt(text_size, 0);
				
				for (std::size_t i(0); i < text_size; ++i)
				{
					auto const pos(sa_buf[i]);
					auto const idx(pos ? pos - 1 : text.size() - 1);
					libbio_assert_lt(idx, text.size());
					//libbio_assert_lt(i, bwt.size()); // Not true for int_vector_buffer.
					auto const cc(text[idx]);
					bwt[i] = cc;
					//std::cerr << i << '\t' << pos << '\t' << text[idx] << '\t' << int(text[idx]) << '\n';
				}

				{
					std::ofstream bwt_stream(fname);
					std::copy(bwt.begin(), bwt.end(), std::ostream_iterator <char>(bwt_stream));
				}
				
				config.file_map[sdsl::conf::KEY_BWT] = fname;
				bwt_path = config.file_map[sdsl::conf::KEY_BWT].data();
			}
		}
		
		// SDSL’s construct() does not use the given text if KEY_TEXT is set in config.
		lb::log_time(std::cerr) << "Building CSA…\n"; 
		sdsl::construct(csa, text_path, config, 1);
	}
}


namespace founder_graphs::founder_graph_indices {
	
	bool dispatch_concurrent_builder::fill_w_x_rho_values(
		founder_block_vector const &block_contents,
		std::size_t const node_count,
		std::size_t const edge_count
	)
	{
		auto group(*m_group_ptr);
		auto sema(*m_sema_ptr);
		auto concurrent_queue(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
		
		libbio::atomic_bit_vector w_positions(edge_count);
		// If I read the reference correctly, the vectors should be zero-filled in C++20, but make sure anyway.
		for (auto &word : w_positions.word_range()) word.store(0, std::memory_order_release);
		
		// Fill the W values and collect segment lengths for X and rho values.
		std::vector <segment_length_description> segment_lengths(node_count);
		std::size_t length_idx{};
		std::atomic_bool status{true};
		for (auto const &[block_idx, block] : rsv::enumerate(block_contents))
		{
			for (auto const &[segment_idx, segment] : rsv::enumerate(block.segments))
			{
				// Search for segment in background.
				dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
				if (!status.load(std::memory_order_acquire))
					goto exit_loop;
				
				lb::dispatch_group_async_fn(
					group,
					concurrent_queue,
					[
						this,
						block_idx = block_idx,
						segment_idx = segment_idx,
						length_idx = length_idx++,
						edge_count,
						&w_positions,
						&segment_lengths,
						&segment = segment,
						&status
					](){
						dispatch_semaphore_guard guard(*m_sema_ptr); // Signal the semaphore when done.
						
						lexicographic_range lex_range(m_index.m_csa);
						size_type res{};
						
						auto const stop_and_call_delegate([this, block_idx, segment_idx, &segment, &status](auto const cc, auto const i){
							m_delegate.zero_occurrences_for_segment(block_idx, segment_idx, segment, cc, i);
							status.store(false, std::memory_order_relaxed);
						});
						
						for (auto const [i, cc] : rsv::enumerate(segment))
						{
							// The CSA is constructed from the reverse of the text, so we can forward-search using backward_search.
							res = lex_range.backward_search(m_index.m_csa, cc);
							if (0 == res)
							{
								stop_and_call_delegate(cc, i);
								return;
							}
						}
						
						res = lex_range.backward_search(m_index.m_csa, '#');
						if (0 == res)
						{
							stop_and_call_delegate('#', segment.size());
							return;
						}
						
						if (1 != res)
						{
							m_delegate.unexpected_number_of_occurrences_for_segment(block_idx, segment_idx, segment, 1, res);
							status.store(false, std::memory_order_relaxed);
							return;
						}
						
						libbio_assert_lte(2, lex_range.lb); // $ and #$ should be smaller than the other segments.
						libbio_assert_lt(lex_range.lb, 2 + edge_count);
						w_positions.fetch_or(lex_range.lb - 2, 0x1, std::memory_order_relaxed);
						
						// No locking needed b.c. no two threads access the same element.
						segment_lengths[length_idx] = {lex_range.lb - 2, segment.size(), block_idx};
					}
				);
			}
		}
		
	exit_loop:
		dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
		if (!status.load(std::memory_order_acquire))
			return false;
		
		m_index.m_w_positions.resize(edge_count, 0);
		copy_words(w_positions, m_index.m_w_positions);
		
		// X values.
		std::sort(segment_lengths.begin(), segment_lengths.end(), [](auto const &lhs, auto const &rhs){
			return lhs.lexicographic_rank < rhs.lexicographic_rank;
		});
		auto const segment_length_sum(ranges::accumulate(
			segment_lengths,
			std::size_t(0), [](std::size_t const length, auto const &length_desc){
				return length + length_desc.segment_length;
			}
		));
		
		m_index.m_x_values.resize(1 + node_count + segment_length_sum, 0);
		
		{
			std::size_t pos(0);
			m_index.m_x_values[0] = 0x1;
			for (auto const &length_desc : segment_lengths)
			{
				pos += 1 + length_desc.segment_length;
				m_index.m_x_values[pos] = 0x1;
			}
		}
		
		// Rho.
		m_index.m_rho_values.width(lb::bits::highest_bit_set(node_count));
		m_index.m_inverse_rho_values.width(lb::bits::highest_bit_set(node_count));
		m_index.m_rho_values.resize(node_count, 0);
		m_index.m_inverse_rho_values.resize(node_count, 0);
		ranges::sort(segment_lengths, ranges::less{}, [](auto const &length_desc){
			return std::make_tuple(length_desc.block_index, length_desc.lexicographic_rank);
		});
		for (auto const &[i, length_desc] : rsv::enumerate(segment_lengths))
			m_index.m_inverse_rho_values[i] = length_desc.lexicographic_rank;
		for (auto const &[i, val] : rsv::enumerate(m_index.m_inverse_rho_values))
			m_index.m_rho_values[val] = i;
		
		return true;
	}
	
	
	bool dispatch_concurrent_builder::fill_be_lr_values(
		founder_block_vector const &block_contents,
		std::size_t const node_count,
		std::size_t const edge_count,
		std::size_t const max_block_height
	)
	{
		auto group(*m_group_ptr);
		auto sema(*m_sema_ptr);
		auto concurrent_queue(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
		
		auto const h_bits(lb::bits::highest_bit_set(2 * max_block_height));
		auto const node_bits(lb::bits::highest_bit_set(node_count));
		auto const node_bits_2(lb::bits::highest_bit_set(2 * node_count));
		auto const lr_high_bits(lb::bits::highest_bit_set(edge_count) - 1); // Max. value is edge_count - 1.
		
		// B and E bit vectors and the L’ and R’ vectors of Lemma 4.6 (and Lemma 4.3).
		// Start with the lexicographic and co-lexicographic ranges of “#” to determine the values for r’ and f(w).
		lexicographic_range_pair base_range(m_index.m_csa);
		{
			auto const res(base_range.backward_search(m_index.m_csa, '#'));
			libbio_assert_neq(0, res);
		}
		
		// Since the positions in B and E are expected to be at least somewhat far apart and
		// setting the values does not require specific order, just consistency, we use
		// atomic bit vectors instead of a serial callback queue. This should be (mostly) fine b.c.
		// cache line size on x86-64 is 64 bytes and on M1 128 bytes.
		lb::atomic_bit_vector b_positions(m_index.m_csa.size());
		lb::atomic_bit_vector e_positions(m_index.m_csa.size());
		// If I read the reference correctly, the vectors should be zero-filled in C++20, but make sure anyway.
		for (auto &word : b_positions.word_range()) word.store(0, std::memory_order_release);
		for (auto &word : e_positions.word_range()) word.store(0, std::memory_order_release);
		
		// We use std::vector here b.c. we would like to access the elements without locking.
		typedef std::uint32_t lr_value_type;
		std::vector <lr_value_type> l_values(edge_count, 0);
		std::vector <lr_value_type> r_values(edge_count, 0);
		
		// Process the segments.
		std::atomic_bool status{true};
		std::size_t block_first_node_idx{};
		for (auto const &[block_idx, block] : rsv::enumerate(block_contents))
		{
			auto const &prefix_counts(block.prefix_counts);
			auto const &edge_count_csum(block.edge_count_csum);
			for (auto const &[segment_idx, segment] : rsv::enumerate(block.segments))
			{
				auto const prefix_count(prefix_counts[segment_idx]);
				auto const expected_occurrence_count(edge_count_csum[prefix_count + 1 + segment_idx] - edge_count_csum[segment_idx]);
				
				// Wait until enough segments have been processed.
				// Stop if an error occurred.
				dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
				if (!status.load(std::memory_order_acquire))
					goto exit_loop;
				
				// Search for the current segment in background.
				lb::dispatch_group_async_fn(
					group,
					concurrent_queue,
					[
						this,
						sema,
						h_bits,
						base_range,
						block_idx = block_idx,
						block_first_node_idx,
						segment_idx = segment_idx,
						expected_occurrence_count,
						&b_positions,
						&e_positions,
						&l_values,
						&r_values,
						&segment = segment,
						&status
					](){
						dispatch_semaphore_guard guard(sema); // Signal the semaphore when done.
						
						// Since we only have the segments and not the edges at this point, we first
						// search for the current segment and then resort to wavelet tree traversal
						// to determine the remaining characters.
						
						lexicographic_range range(m_index.m_csa);
						lexicographic_range_pair range_(base_range);
						
						auto const stop_and_call_delegate([this, block_idx, segment_idx, &segment, &status](auto const cc, auto const i){
							m_delegate.zero_occurrences_for_segment(block_idx, segment_idx, segment, cc, i);
							status.store(false, std::memory_order_relaxed);
						});
						
						// Start with v (since we search in the forward direction).
						{
							size_type res{};
							for (auto const [i, cc] : rsv::enumerate(segment))
							{
								res = range.backward_search(m_index.m_csa, cc);
								auto const res_(range_.backward_search(m_index.m_csa, cc));
							
								if (0 == res || 0 == res_)
								{
									stop_and_call_delegate(cc, i);
									return;
								}
							}
						
							// At this point we can use range to fill B and E.
							// Sanity check.
							if (expected_occurrence_count != res)
							{
								m_delegate.unexpected_number_of_occurrences_for_segment(block_idx, segment_idx, segment, expected_occurrence_count, res);
								status.store(false, std::memory_order_relaxed);
								return;
							}
						}
						
						// Set the values for B and E.
						{
							auto const b_res(b_positions.fetch_or(range.lb, 0x1, std::memory_order_relaxed));
							auto const e_res(e_positions.fetch_or(range.rb, 0x1, std::memory_order_relaxed));
							
							if (b_res)
							{
								m_delegate.position_in_b_already_set(range.lb);
								status.store(false, std::memory_order_relaxed);
								return;
							}
							
							if (e_res)
							{
								m_delegate.position_in_e_already_set(range.rb);
								status.store(false, std::memory_order_relaxed);
								return;
							}
						}
						
						// For determining f(v).
						auto v_range(range);
						if (0 == v_range.backward_search(m_index.m_csa, '#'))
						{
							stop_and_call_delegate('#', segment.size());
							return;
						}
						
						// Continue with interval_symbols.
						// Maintain a stack of lexicographic ranges and use backtracking.
						// Unfortunately the values for R’ need to be handled separately to
						// handle the overlaps as shown in Lemma 4.5.
						interval_symbols_context is_ctx;
						std::vector <std::pair <lexicographic_range, lexicographic_range_pair>> stack;
						typedef std::vector <std::pair <size_type, lr_value_type>> r_value_list; // pairs (r’, f(w)).
						r_value_list r_values_;
						stack.emplace_back(range, range_);
						while (!stack.empty())
						{
							auto pair(stack.back()); // Copy.
							stack.pop_back();
							
							auto &range(pair.first);
							auto &range_(pair.second);
							auto const sym_count(range.interval_symbols(m_index.m_csa, is_ctx));
							for (std::size_t i(0); i < sym_count; ++i)
							{
								// Copy.
								auto new_range(range);
								auto new_range_(range_);
								
								auto const cc(is_ctx.cs[i]);
								new_range.backward_search(m_index.m_csa, cc);
								new_range_.backward_search(m_index.m_csa, cc);
								
								// Check for the sentinel.
								if ('#' == cc)
								{
									// Now we have a complete edge.
									// Sanity checks.
									libbio_assert_eq(1, new_range.size());
									libbio_assert_eq(1, new_range_.range.size());
									libbio_assert_eq(1, new_range_.co_range.size());
									libbio_assert_eq(1, v_range.size());
									libbio_assert_lte(2, v_range.lb);
									libbio_assert_lte(2, new_range.lb);
									
									// r and r’
									auto const rr(new_range.lb);
									auto const rr_(new_range_.co_range.lb);
									
									// f(v) and f(w)
									// rank takes a half-open range, we take $ and #$ into account.
									auto const fv(m_index.m_w_positions_rank1_support(v_range.lb - 1));
									auto const fw(m_index.m_w_positions_rank1_support(new_range.lb - 1));
									auto const rfv(m_index.m_rho_values[fv]);
									auto const rfw(m_index.m_rho_values[fw]);
									libbio_assert_lt(rfv, rfw);
									lr_value_type const diff(rfw - rfv);
									
									l_values[rr] = (fv << h_bits) | diff;
									r_values_.emplace_back(rr_, diff);
								}
								else
								{
									stack.emplace_back(new_range, new_range_);
								}
							}
						}
						
						// Handle the R’ values.
						{
							lr_value_type list_identifier(2 * block_first_node_idx);
							std::sort(r_values_.begin(), r_values_.end(), [](auto const &lhs, auto const &rhs){
								return lhs.first < rhs.first;
							});
							
							lr_value_type prev_value{};
							for (auto &pair : r_values_)
							{
								libbio_assert_neq(prev_value, pair.second);
								if (! (prev_value < pair.second))
									++list_identifier;
								
								prev_value = pair.second;
								pair.second |= list_identifier << h_bits;
								
								// Store.
								r_values[pair.first] = pair.second;
							}
						}
					}
				);
			}
			
			block_first_node_idx += block.segments.size();
		}
		
	exit_loop:
		dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
		if (!status.load(std::memory_order_acquire))
			return false;
		
		// Copy the values.
		{
			auto const size(m_index.m_csa.size());
			
			m_index.m_b_positions.resize(size, 0);
			m_index.m_e_positions.resize(size, 0);
			
			copy_words(b_positions, m_index.m_b_positions);
			copy_words(e_positions, m_index.m_e_positions);
		}
		
		{
			m_index.m_l_values = elias_inventory(l_values, h_bits + node_bits   - lr_high_bits);
			m_index.m_r_values = elias_inventory(r_values, h_bits + node_bits_2 - lr_high_bits);
		}
		
		return true;
	}
	
	
	void dispatch_concurrent_builder::prepare_rank_and_select_support()
	{
		auto group(*m_group_ptr);
		auto concurrent_queue(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
		
		dispatch_group_async(group, concurrent_queue, ^{
			sdsl::util::init_support(m_index.m_b_positions_rank1_support, &m_index.m_b_positions);
		});
		dispatch_group_async(group, concurrent_queue, ^{
			sdsl::util::init_support(m_index.m_b_positions_select1_support, &m_index.m_b_positions);
		});
		dispatch_group_async(group, concurrent_queue, ^{
			sdsl::util::init_support(m_index.m_e_positions_select1_support, &m_index.m_e_positions);
		});
		dispatch_group_async(group, concurrent_queue, ^{
			sdsl::util::init_support(m_index.m_x_values_select1_support, &m_index.m_w_positions);
		});
		dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
	}
}


namespace founder_graphs {
	
	void founder_graph_index::build_csa(
		std::string const &text_path,
		char const *sa_path,
		char const *bwt_path,
		bool const text_is_zero_terminated
	)
	{
		// Build the CSA.
		construct_csa(m_csa, text_path, sa_path, bwt_path, text_is_zero_terminated);
	}

	bool founder_graph_index::store_node_label_lexicographic_ranges(
		std::string const &block_content_path,
		founder_graph_index_construction_delegate &delegate
	)
	{
		// Construct the founder graph index.
		// After preparing the BWT index, we read the block contents (i.e. segments), try to locate them in the index
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
		
		// Read the segments. In order to read the input just once, we unfortunately have to buffer everything.
		std::vector <fgi::founder_block> block_contents;
		fgi::dispatch_concurrent_builder builder(*this, delegate);
		fg::length_type max_block_height{};
		
		{
			lb::file_istream block_content_stream;
			lb::open_file_for_reading(block_content_path, block_content_stream);
			cereal::PortableBinaryInputArchive archive(block_content_stream);
			
			fg::length_type block_count{};
			archive(cereal::make_size_tag(block_count));
			block_contents.resize(block_count);
			
			for (auto &block : block_contents)
			{
				// Read the segment count.
				fg::length_type segment_count{};
				archive(cereal::make_size_tag(segment_count));
				
				max_block_height = std::max(max_block_height, segment_count);
				
				// Resize the vectors.
				block.segments.resize(segment_count);
				block.prefix_counts.resize(segment_count, 0);
				block.edge_count_csum.resize(1 + segment_count, 0);
				
				// Read the current block and calculate the cumulative sum of edge counts.
				for (fg::length_type j(0); j < segment_count; ++j)
				{
					archive(block.segments[j]);
					archive(block.prefix_counts[j]);
					archive(block.edge_count_csum[1 + j]);
				}
				
				// Sort by lexicographic order of the segments.
				// Needed later by the construction algorithm.
				ranges::sort(rsv::zip(block.segments, block.prefix_counts, block.edge_count_csum));
				
				// Calculate the cumulative sum.
				for (fg::length_type j(0); j < segment_count; ++j)
					block.edge_count_csum[1 + j] += block.edge_count_csum[j];
			}
		}
		
		typedef std::pair <std::size_t, std::size_t> count_pair;
		auto const [node_count, edge_count] = ranges::accumulate(
			block_contents,
			count_pair(0, 0),
			[](auto const sum_pair, auto const &block) -> count_pair {
				return {sum_pair.first + block.segments.size(), sum_pair.second + block.edge_count_csum.back()};
			}
		);
		
		// Prepare the W bit vector.
		if (!builder.fill_w_x_rho_values(block_contents, node_count, edge_count))
			return false;
		
		// Rank_1 support for W is needed at this point.
		sdsl::util::init_support(m_w_positions_rank1_support, &m_w_positions);
		
		if (!builder.fill_be_lr_values(block_contents, node_count, edge_count, max_block_height))
			return false;
		
		// Prepare rank and select support.
		builder.prepare_rank_and_select_support();
		
		return true;
	}
}
