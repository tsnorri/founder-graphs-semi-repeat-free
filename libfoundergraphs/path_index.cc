/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <founder_graphs/founder_graph_indices/path_index.hh>


namespace founder_graphs::founder_graph_indices {

	std::uint64_t path_index::serialize(std::ostream &os, sdsl::structure_tree_node *v, std::string name) const
	{
		return sdsl_serialize(*this, name, v, os);
	}
	
	
	std::uint64_t path_index_support::serialize(std::ostream &os, sdsl::structure_tree_node *v, std::string name) const
	{
		return sdsl_serialize(*this, name, v, os);
	}
	
	
	void path_index::combine_node_paths_left(
		co_lexicographic_range const co_range,
		size_type const rhs_node_idx,
		sdsl::bit_vector &head_buffer
	) const
	{
		// co_range is the co-lexicographic range of some ℓ(v, w)#.
		head_buffer.assign(m_s.input_count, 0);
		
		for (auto colex_rank(co_range.lb); colex_rank < co_range.rb + 1; ++colex_rank)
		{
			auto const rho_diff(m_s.l[colex_rank]);
			auto const lhs_node_idx(rhs_node_idx - rho_diff);
			process_u <std::bit_or <std::uint64_t>>(head_buffer, lhs_node_idx);
		}
	}
	
	
	void path_index::combine_node_paths_left_multiple(
		co_lexicographic_range const co_range,
		size_type const prev_node_count,
		sdsl::bit_vector &head_buffer
	) const
	{
		// co_range is the co-lexicographic range of some ℓ(v, w)#.
		head_buffer.assign(m_s.input_count, 0);
		
		for (auto colex_rank(co_range.lb); colex_rank < co_range.rb + 1; ++colex_rank)
		{
			auto const rhs_node_rank(m_s.a_tilde[colex_rank]);
			auto const rhs_node_idx(prev_node_count + rhs_node_rank);
			auto const rho_diff(m_s.l[colex_rank]);
			auto const lhs_node_idx(rhs_node_idx - rho_diff);
			process_u <std::bit_or <std::uint64_t>>(head_buffer, lhs_node_idx);
		}
	}
	
	
	void path_index::combine_node_paths_right(
		size_type const first_lb,
		size_type const first_rb,
		size_type const prev_node_count,
		sdsl::bit_vector &tail_buffer
	) const
	{
		auto const d0(m_s.d_rank1_support(first_lb)); // We add edge_count later.
		auto const lex_range_size(first_rb - first_lb + 1);
		
		tail_buffer.assign(m_s.input_count, 0);
		size_type edge_count(0);
		for (size_type i(0); i < lex_range_size; ++i)
		{
			// Check that we are on the left side of an edge.
			if (!m_s.d[first_lb + i])
				continue;
			
			++edge_count;
			auto const alpha(d0 + edge_count - 1);
			libbio_assert_eq(alpha, m_s.d_rank1_support(first_lb + i + 1) - 1);
			auto const rho_diff(m_s.r[alpha]);
			auto const lhs_rank(prev_node_count + m_s.a[alpha]);
			auto const rhs_rank(lhs_rank + rho_diff);
			process_u <std::bit_or <std::uint64_t>, const_op <std::uint64_t>, std::bit_and <std::uint64_t>>(tail_buffer, lhs_rank, rhs_rank);
		}
	}
}
