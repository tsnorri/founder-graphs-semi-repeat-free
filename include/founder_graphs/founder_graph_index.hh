/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_FOUNDER_GRAPH_INDEX_HH
#define FOUNDER_GRAPHS_FOUNDER_GRAPH_INDEX_HH

#include <limits>
#include <ostream>
#include <sdsl/csa_wt.hpp>
#include <sdsl/suffix_array_algorithm.hpp>


namespace founder_graphs::founder_graph_indices { 
	
	typedef sdsl::wt_huff <
		sdsl::bit_vector,
		sdsl::rank_support_v5 <1>,
		sdsl::select_support_mcl <1>,
		sdsl::select_support_mcl <0>
	> wavelet_tree_type;
	
	typedef sdsl::csa_wt <
		wavelet_tree_type,
		1 << 30,			// Sample density for suffix array (SA) values
		1 << 30				// Sample density for inverse suffix array (ISA) values
	> csa_type;
}

namespace founder_graphs {
	
	struct founder_graph_index_construction_delegate
	{
		virtual ~founder_graph_index_construction_delegate() {}
		
		virtual void zero_occurrences_for_segment(
			length_type const block_idx,
			length_type const seg_idx,
			std::string const &segment,
			char const cc,
			length_type const pos
		) = 0;
		
		virtual void unexpected_number_of_occurrences_for_segment(
			length_type const block_idx,
			length_type const seg_idx,
			std::string const &segment,
			length_type const expected_count,
			length_type const actual_count
		) = 0;
		
		virtual void position_in_b_already_set(length_type const pos) = 0;
		virtual void position_in_e_already_set(length_type const pos) = 0;
	};
	
	
	class founder_graph_index
	{
	public:
		typedef founder_graph_indices::csa_type	csa_type;
		typedef sdsl::bit_vector				bit_vector_type;
		typedef sdsl::rank_support_v5 <1>		rank1_support_type;
		typedef sdsl::select_support_mcl <1>	select1_support_type;
		typedef csa_type::size_type				size_type;
		
	protected:
		csa_type				m_csa;
		bit_vector_type			m_b_positions;
		bit_vector_type			m_e_positions;
		rank1_support_type		m_b_positions_rank1_support;
		select1_support_type	m_b_positions_select1_support;
		select1_support_type	m_e_positions_select1_support;
		
	public:
		bool construct(
			std::string const &text_path,
			char const *sa_path,
			std::string const &block_content_path,
			bool const text_is_zero_terminated,
			founder_graph_index_construction_delegate &delegate
		);
		
		template <typename t_archive>
		void CEREAL_SAVE_FUNCTION_NAME(t_archive &archive) const;
		
		template <typename t_archive>
		void CEREAL_LOAD_FUNCTION_NAME(t_archive &archive);
		
		template <typename t_rng>
		size_type forward_search(t_rng &&rng, size_type &ll, size_type &rr) const;
		
		template <typename t_rng>
		size_type forward_search(t_rng &&rng) const;
	};
	
	
	template <typename t_archive>
	void founder_graph_index::CEREAL_SAVE_FUNCTION_NAME(t_archive &archive) const
	{
		archive(CEREAL_NVP(m_csa));
		archive(CEREAL_NVP(m_b_positions));
		archive(CEREAL_NVP(m_e_positions));
		archive(CEREAL_NVP(m_b_positions_rank1_support));
		archive(CEREAL_NVP(m_b_positions_select1_support));
		archive(CEREAL_NVP(m_e_positions_select1_support));
	}
	
	
	template <typename t_archive>
	void founder_graph_index::CEREAL_LOAD_FUNCTION_NAME(t_archive &archive)
	{
		archive(CEREAL_NVP(m_csa));
		archive(CEREAL_NVP(m_b_positions));
		archive(CEREAL_NVP(m_e_positions));
		archive(CEREAL_NVP(m_b_positions_rank1_support));
		archive(CEREAL_NVP(m_b_positions_select1_support));
		archive(CEREAL_NVP(m_e_positions_select1_support));
		m_b_positions_rank1_support.set_vector(&m_b_positions);
		m_b_positions_select1_support.set_vector(&m_b_positions);
		m_e_positions_select1_support.set_vector(&m_e_positions);
	}
	
	
	template <typename t_rng>
	auto founder_graph_index::forward_search(t_rng &&rng, size_type &ll, size_type &rr) const -> size_type
	{
		size_type res{};
		for (auto const cc : rng)
		{
			res = sdsl::backward_search(m_csa, ll, rr, cc, ll, rr);
			if (res)
				continue;
			
			if ('#' == m_csa.bwt[ll])
			{
				auto const qq(m_b_positions_rank1_support(1 + ll));
				size_type const ll_(m_b_positions_select1_support(qq));
				size_type const rr_(m_e_positions_select1_support(qq));
				if (ll_ <= ll && rr <= rr_)
				{
					res = sdsl::backward_search(m_csa, ll_, rr_, cc, ll, rr);
					if (res)
						continue;
				}
			}
			
			return 0;
		}
		return res;
	}
	
	
	template <typename t_rng>
	auto founder_graph_index::forward_search(t_rng &&rng) const -> size_type
	{
		size_type ll{};
		size_type rr{m_csa.size()};
		return forward_search(rng, ll, rr);
	}
}

#endif
