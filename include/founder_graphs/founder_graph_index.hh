/*
 * Copyright (c) 2021-2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_FOUNDER_GRAPH_INDEX_HH
#define FOUNDER_GRAPHS_FOUNDER_GRAPH_INDEX_HH

#include <founder_graphs/elias_inventory.hh>
#include <libbio/dispatch.hh>
#include <limits>
#include <ostream>
#include <sdsl/csa_wt.hpp>
#include <sdsl/suffix_array_algorithm.hpp>


namespace founder_graphs {
	
	class founder_graph_index; // Fwd.
	
	
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
}


namespace founder_graphs::founder_graph_indices { 
	
	typedef sdsl::wt_int <
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
	
	
	struct founder_block
	{
		std::vector <std::string>	segments;
		std::vector <length_type>	prefix_counts;
		std::vector <length_type>	edge_count_csum;
	};
	
	typedef std::vector <founder_block>	founder_block_vector;
	
	
	class dispatch_concurrent_builder
	{
	public:
		typedef csa_type::size_type				size_type;
		
	protected:
		libbio::dispatch_ptr <dispatch_group_t>		m_group_ptr;
		libbio::dispatch_ptr <dispatch_semaphore_t>	m_sema_ptr;		// Limit the number of segments read in the current thread.
		founder_graph_index							&m_index;
		founder_graph_index_construction_delegate	&m_delegate;
	
	public:
		dispatch_concurrent_builder(founder_graph_index &index, founder_graph_index_construction_delegate &delegate):
			m_group_ptr(dispatch_group_create()),
			m_sema_ptr(dispatch_semaphore_create(256)),
			m_index(index),
			m_delegate(delegate)
		{
		}
		
		bool fill_w_x_rho_values(
			founder_block_vector const &block_contents,
			std::size_t const node_count,
			std::size_t const edge_count
		);
		
		bool fill_be_lr_values(
			founder_block_vector const &block_contents,
			std::size_t const node_count,
			std::size_t const edge_count,
			std::size_t const max_block_height
		);
		
		void prepare_rank_and_select_support();
	};
}


namespace founder_graphs {
	
	class founder_graph_index
	{
		friend class founder_graph_indices::dispatch_concurrent_builder;
		
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
		bit_vector_type			m_w_positions;
		bit_vector_type			m_x_values;
		sdsl::int_vector <0>	m_rho_values;
		sdsl::int_vector <0>	m_inverse_rho_values;
		elias_inventory			m_l_values;
		elias_inventory			m_r_values;
		rank1_support_type		m_b_positions_rank1_support;
		rank1_support_type		m_w_positions_rank1_support;
		select1_support_type	m_b_positions_select1_support;
		select1_support_type	m_e_positions_select1_support;
		select1_support_type	m_x_values_select1_support;
		
	public:
		void build_csa(
			std::string const &text_path,
			char const *sa_path,
			char const *bwt_path,
			bool const text_is_zero_terminated
		);

		bool store_node_label_lexicographic_ranges(
			std::string const &block_content_path,
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
		archive(CEREAL_NVP(m_w_positions));
		archive(CEREAL_NVP(m_x_values));
		archive(CEREAL_NVP(m_rho_values));
		archive(CEREAL_NVP(m_inverse_rho_values));
		archive(CEREAL_NVP(m_l_values));
		archive(CEREAL_NVP(m_r_values));
		archive(CEREAL_NVP(m_b_positions_rank1_support));
		archive(CEREAL_NVP(m_w_positions_rank1_support));
		archive(CEREAL_NVP(m_b_positions_select1_support));
		archive(CEREAL_NVP(m_e_positions_select1_support));
		archive(CEREAL_NVP(m_x_values_select1_support));
	}
	
	
	template <typename t_archive>
	void founder_graph_index::CEREAL_LOAD_FUNCTION_NAME(t_archive &archive)
	{
		archive(CEREAL_NVP(m_csa));
		archive(CEREAL_NVP(m_b_positions));
		archive(CEREAL_NVP(m_e_positions));
		archive(CEREAL_NVP(m_w_positions));
		archive(CEREAL_NVP(m_x_values));
		archive(CEREAL_NVP(m_rho_values));
		archive(CEREAL_NVP(m_inverse_rho_values));
		archive(CEREAL_NVP(m_l_values));
		archive(CEREAL_NVP(m_r_values));
		archive(CEREAL_NVP(m_b_positions_rank1_support));
		archive(CEREAL_NVP(m_w_positions_rank1_support));
		archive(CEREAL_NVP(m_b_positions_select1_support));
		archive(CEREAL_NVP(m_e_positions_select1_support));
		archive(CEREAL_NVP(m_x_values_select1_support));
		m_b_positions_rank1_support.set_vector(&m_b_positions);
		m_w_positions_rank1_support.set_vector(&m_w_positions);
		m_b_positions_select1_support.set_vector(&m_b_positions);
		m_e_positions_select1_support.set_vector(&m_e_positions);
		m_x_values_select1_support.set_vector(&m_x_values);
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
