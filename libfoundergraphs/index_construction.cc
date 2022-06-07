/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <founder_graphs/founder_graph_indices/index_construction.hh>
#include <founder_graphs/utility.hh>
#include <libbio/assert.hh>
#include <libbio/utility.hh>
#include <range/v3/algorithm/is_sorted.hpp>
#include <range/v3/algorithm/lexicographical_compare.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/reverse.hpp>


namespace fg  = founder_graphs;
namespace fgi = founder_graphs::founder_graph_indices;
namespace lb  = libbio;
namespace rsv = ranges::views;


namespace {
	
	// XXX Currently not in use.
	template <typename t_csa>
	void construct_csa_(
		t_csa &csa,
		std::string const &text_path,
		char const *sa_path,
		char const *bwt_path,
		bool const text_is_zero_terminated
	)
	{
		// FIXME: remove logging or use a delegate.

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
	
	
	void bedinx_handle_prefix(
		std::string const &seg,
		fgi::lexicographic_range_pair const range_pair,
		std::size_t const block_idx,
		fgi::bedinx_values_buffer &dst
	)
	{
		// |ℬ| need not be equal to |I|.
		libbio_assert_eq(dst.b_positions.size(), dst.e_positions.size());
		libbio_assert_eq(dst.b_positions.size(), dst.shortest_prefix_lengths.size());
		libbio_assert_eq(dst.b_positions.size(), dst.block_numbers.size());
		
		fg::push_back(dst.b_positions, range_pair.range.lb);
		fg::push_back(dst.e_positions, range_pair.range.rb);
		fg::push_back(dst.i_positions, range_pair.co_range.lb);
		fg::push_back(dst.shortest_prefix_lengths, seg.size());
		fg::push_back(dst.block_numbers, block_idx);
	}
	
	
	void bedinx_update_u(
		fgi::input_map const &inputs,
		std::size_t const node_base,
		std::size_t const u_row_size_,
		sdsl::bit_vector &dst
	)
	{
		for (auto const &kv : inputs) // Segment numbers to input numbers.
		{
			auto const node_idx(kv.first);
			auto const input_idx(kv.second);
			auto const idx((node_base + node_idx) * u_row_size_ + input_idx);
			dst[idx] = 1;
			//std::cerr << "dst:    " << (&dst) << " base: " << node_base << " node: " << node_idx << " input: " << input_idx << " idx: " << idx << '\n';
		}
	}
	
	
	inline void alr_update_dst(
		fg::count_type const lhs,
		fg::count_type const rhs,
		fg::count_type const lhs_height,
		fgi::lexicographic_range const range,
		fgi::co_lexicographic_range const co_range,
		fgi::rank_support_type <fgi::path_index_support_base::d_bit_vector_type, 1> const &d_rank1_support,
		fgi::alr_values_buffer &dst
	)
	{
		auto const alpha_val_(d_rank1_support(1 + range.lb));
		libbio_assert_lt(0, alpha_val_);
		auto const alpha_val(alpha_val_ - 1);
		auto const alpha_tilde_val(co_range.lb);
		auto const rho_diff(rhs + lhs_height - lhs);
		
		fg::push_back(dst.alpha_values, alpha_val);
		fg::push_back(dst.alpha_tilde_values, alpha_tilde_val);
		fg::push_back(dst.a_values, lhs);
		fg::push_back(dst.a_tilde_values, rhs);
		fg::push_back(dst.lr_values, rho_diff);
	}
}


namespace founder_graphs::founder_graph_indices {
	
#if 0
	void construct_csa(
		csa_type &csa,
		std::string const &text_path,
		char const *sa_path,
		char const *bwt_path,
		bool const text_is_zero_terminated
	)
	{
		construct_csa_(csa, text_path, sa_path, bwt_path, text_is_zero_terminated);
	}
	
	
	void construct_reverse_csa(
		reverse_csa_type &csa,
		std::string const &text_path,
		char const *sa_path,
		char const *bwt_path,
		bool const text_is_zero_terminated
	)
	{
		// FIXME: write me.
	}
#endif
	
	
	void bedinx_values_buffer::reset()
	{
		b_positions.clear();
		e_positions.clear();
		d_positions.clear();
		i_positions.clear();
		shortest_prefix_lengths.clear();
		block_numbers.clear();
		u_values.clear();
	}
	
	
	void alr_values_buffer::reset()
	{
		alpha_values.clear();
		alpha_tilde_values.clear();
		a_values.clear();
		a_tilde_values.clear();
		lr_values.clear();
	}
	
	
	void bedinx_set_positions_for_range(
		csa_type const &csa,
		reverse_csa_type const &reverse_csa,
		block_graph const &gr,
		std::size_t const u_row_size_,
		std::size_t block_idx,
		std::size_t const block_end,
		bedinx_values_buffer &dst
	)
	{
		auto const &blocks(gr.blocks);
		
		libbio_assert_lt(block_end, blocks.size()); // We must always be able to access the sentinel to be able to calculate the node count in the range.
		auto const node_count(blocks[block_end].node_csum - blocks[block_idx].node_csum);
		
		// Clear the destination.
		dst.reset();
		
		// Allocate memory for dst.u_values.
		dst.u_values.clear();
		dst.u_values.assign(u_row_size_ * node_count, 0);
		
		if (block_idx < block_end)
		{
			// Special case for the first block, since its nodes do not have any in-edges.
			std::size_t node_base(0);
			if (0 == block_idx)
			{
				auto const &block(blocks.front());
				libbio_assert(!block.segments.empty());
				libbio_assert(ranges::is_sorted(block.segments));
				
				// Since the segments are lexicographically sorted, we can
				// compare the head of every tail of the list to the subsequent items.
				auto const &first_seg(block.segments.front());
				lexicographic_range_pair prefix_range_pair(csa, reverse_csa);
				prefix_range_pair.backward_search(csa, reverse_csa, first_seg.begin(), first_seg.end());
				libbio_assert(!prefix_range_pair.empty());
				bedinx_handle_prefix(first_seg, prefix_range_pair, block_idx, dst);
				for (auto const &seg : rsv::tail(block.segments))
				{
					lexicographic_range_pair range_pair(csa, reverse_csa);
					range_pair.backward_search(csa, reverse_csa, seg.begin(), seg.end());
					libbio_assert(!range_pair.empty());
					if (range_pair.has_prefix(prefix_range_pair))
					{
						// Store the left bound of the co-lexicographic range
						// (corresponds to #l(v)) of every segment.
						push_back(dst.i_positions, range_pair.co_range.lb);
					}
					else
					{
						// New prefix found.
						prefix_range_pair = range_pair;
						bedinx_handle_prefix(seg, prefix_range_pair, block_idx, dst);
					}
				}
				
				bedinx_update_u(block.inputs, node_base, u_row_size_, dst.u_values);
				node_base += block.segments.size();
				++block_idx;
			}
			
			// General case.
			// This works both with and without 2-dimensional range queries b.c. the co-lexicographic
			// range is not used after processing the right hand segment. (Otherwise the left hand
			// segment would have to be forward-searched at the same time.)
			for (; block_idx < block_end; ++block_idx)
			{
				auto const &prev_block(blocks[block_idx - 1]);
				auto const &block(blocks[block_idx]);
				libbio_assert(!block.segments.empty());
				libbio_assert(ranges::is_sorted(block.segments));
				
				// Process the edges.
				fg::count_type prev_rhs(COUNT_MAX);
				lexicographic_range_pair rhs_prefix_range_pair(CSA_SIZE_MAX, 0, CSA_SIZE_MAX, 0); // Must be some invalid value initially.
				lexicographic_range_pair rhs_range_pair;
				for (auto const &[rhs, lhs] : block.reverse_in_edges) // In rhs order.
				{
					if (rhs != prev_rhs)
					{
						prev_rhs = rhs;
						rhs_range_pair.reset(csa);
						auto const &rhs_seg(block.segments[rhs]);
						rhs_range_pair.backward_search(csa, reverse_csa, rhs_seg.begin(), rhs_seg.end());
						libbio_assert(!rhs_range_pair.empty());
						if (rhs_range_pair.has_prefix(rhs_prefix_range_pair))
						{
							// Store the left bound of the co-lexicographic range
							// (corresponds to #l(v)) of every segment.
							push_back(dst.i_positions, rhs_range_pair.co_range.lb);
						}
						else
						{
							// New prefix found.
							bedinx_handle_prefix(rhs_seg, rhs_range_pair, block_idx, dst);
							rhs_prefix_range_pair = rhs_range_pair;
						}
					}
				
					// For D, we only search for l(v)l(w) and not l(v)l(w)#,
					// but the lexicographic rank of the latter is the first of the range of the former
					// b.c. # is lexicographically smaller than any character except for $.
					lexicographic_range lhs_range(rhs_range_pair.range);
					auto const &lhs_seg(prev_block.segments[lhs]);
					lhs_range.backward_search(csa, lhs_seg.begin(), lhs_seg.end());
					push_back(dst.d_positions, lhs_range.lb);
				}
				
				// Handle U.
				bedinx_update_u(block.inputs, node_base, u_row_size_, dst.u_values);
				node_base += block.segments.size();
			}
		}
	}
	
	
	void alr_values_for_range(
		csa_type const &csa,
		reverse_csa_type const &reverse_csa,
		block_graph const &gr,
		rank_support_type <path_index_support_base::d_bit_vector_type, 1> const &d_rank1_support,
		std::size_t block_idx,
		std::size_t const block_end,
		alr_values_buffer &dst
	)
	{
		libbio_assert_lt(0, block_idx);
		
		constexpr bool const CAN_CONTINUE_BACKWARD_SEARCH_IN_CO_RANGE{lexicographic_range_pair::USES_RANGE_SEARCH_2D};
		
		auto const &blocks(gr.blocks);
		
		for (; block_idx < block_end; ++block_idx)
		{
			auto const &lhs_block(blocks[block_idx - 1]);
			auto const &rhs_block(blocks[block_idx]);
			auto const lhs_height(lhs_block.segments.size());
			
			// Process the edges.
			// We use a small optimization for 2-dimensional range queries.
			// (Similar could be done without if the order of the stored values were considered.)
			if constexpr (CAN_CONTINUE_BACKWARD_SEARCH_IN_CO_RANGE)
			{
				fg::count_type prev_rhs(COUNT_MAX);
				lexicographic_range_pair rhs_range_pair;
				for (auto const &[rhs, lhs] : rhs_block.reverse_in_edges) // In rhs order.
				{
					if (rhs != prev_rhs)
					{
						prev_rhs = rhs;
						rhs_range_pair.reset(csa);
						auto const &rhs_seg(rhs_block.segments[rhs]);
						rhs_range_pair.backward_search_h(csa, reverse_csa, rhs_seg.begin(), rhs_seg.end());
					}
					
					lexicographic_range_pair range_pair(rhs_range_pair);
					auto const &lhs_seg(lhs_block.segments[lhs]);
					range_pair.backward_search(csa, reverse_csa, lhs_seg.begin(), lhs_seg.end());
					libbio_assert(range_pair.is_singleton());
					
					alr_update_dst(lhs, rhs, lhs_height, range_pair.range, range_pair.co_range, d_rank1_support, dst);
				}
			}
			else
			{
				for (auto const &[rhs, lhs] : rhs_block.reverse_in_edges) // In rhs order.
				{
					auto const &rhs_seg(rhs_block.segments[rhs]);
					auto const &lhs_seg(lhs_block.segments[lhs]);
					
					lexicographic_range range(csa);
					co_lexicographic_range co_range(reverse_csa);
					range.backward_search_h(csa, rhs_seg.begin(), rhs_seg.end());
					range.backward_search(csa, lhs_seg.begin(), lhs_seg.end());
					co_range.forward_search(reverse_csa, lhs_seg.begin(), lhs_seg.end());
					co_range.forward_search_h(reverse_csa, rhs_seg.begin(), rhs_seg.end());
					
					libbio_assert(range.is_singleton());
					libbio_assert(co_range.is_singleton());
					
					alr_update_dst(lhs, rhs, lhs_height, range, co_range, d_rank1_support, dst);
				}
			}
		}
	}
}
