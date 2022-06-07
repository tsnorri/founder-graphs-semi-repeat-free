/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_FOUNDER_GRAPH_INDICES_INDEX_CONSTRUCTION_HH
#define FOUNDER_GRAPHS_FOUNDER_GRAPH_INDICES_INDEX_CONSTRUCTION_HH

#include <founder_graphs/founder_graph_indices/block_graph.hh>
#include <founder_graphs/founder_graph_indices/path_index.hh>
#include <libbio/int_matrix.hh>
#include <span>


namespace founder_graphs::founder_graph_indices {
	
#if 0
	// Build a CSA using an indexable text.
	void construct_csa(
		csa_type &csa,
		std::string const &text_path,
		char const *sa_path,
		char const *bwt_path,
		bool const text_is_zero_terminated
	);
	
	
	void construct_reverse_csa(
		reverse_csa_type &reverse_csa,
		std::string const &text_path,
		char const *sa_path,
		char const *bwt_path,
		bool const text_is_zero_terminated
	);
#endif
	
	
	struct bedinx_values_buffer
	{
		// Try to save some space by using only as many bits as needed for each value.
		sdsl::int_vector <0>	b_positions;
		sdsl::int_vector <0>	e_positions;
		sdsl::int_vector <0>	d_positions;
		sdsl::int_vector <0>	i_positions;
		sdsl::int_vector <0>	shortest_prefix_lengths;	// In ℬ order.
		sdsl::int_vector <0>	block_numbers;				// In ℬ order.
		sdsl::bit_vector		u_values;
		
		bedinx_values_buffer() = default;
		
		bedinx_values_buffer(
			std::uint8_t const csa_size_bits,
			std::uint8_t const block_number_bits,
			std::uint8_t const node_label_max_length_bits
		):
			b_positions(0, 0, csa_size_bits),
			e_positions(0, 0, csa_size_bits),
			d_positions(0, 0, csa_size_bits),
			i_positions(0, 0, csa_size_bits),
			shortest_prefix_lengths(0, 0, node_label_max_length_bits),
			block_numbers(0, 0, block_number_bits)
			// Bits needed for U not known at this point.
		{
		}
		
		void reset();
	};
	
	
	struct alr_values_buffer
	{
		// Try to save some space by using only as many bits as needed for each value.
		sdsl::int_vector <0>	alpha_values;		// Keys for A, L
		sdsl::int_vector <0>	alpha_tilde_values;	// Keys for Ã, R
		sdsl::int_vector <0>	a_values;
		sdsl::int_vector <0>	a_tilde_values;
		sdsl::int_vector <0>	lr_values;
		
		alr_values_buffer() = default;
		
		alr_values_buffer(
			std::uint8_t const alpha_bits,
			std::uint8_t const alpha_tilde_bits,
			std::uint8_t const bits_h,
			std::uint8_t const bits_2h
		):
			alpha_values(0, 0, alpha_bits),
			alpha_tilde_values(0, 0, alpha_tilde_bits),
			a_values(0, 0, bits_h),
			a_tilde_values(0, 0, bits_h),
			lr_values(0, 0, bits_2h)
		{
		}
		
		void reset();
	};
	
	
	// Number of bits needed for each node.
	template <std::size_t t_u_block_size>
	inline std::size_t u_row_size(block_graph const &gr)
	{
		return (((gr.input_count + (t_u_block_size - 1)) / t_u_block_size) * t_u_block_size);
	}
	
	
	void bedinx_set_positions_for_range(
		csa_type const &csa,
		reverse_csa_type const &reverse_csa,
		block_graph const &gr,
		std::size_t const u_row_size_,
		std::size_t i,
		std::size_t const end,
		bedinx_values_buffer &dst
	);
	
	
	template <std::size_t t_u_block_size> // Block size for (compressed) bit vectors.
	void bedinx_set_positions_for_range(
		csa_type const &csa,
		reverse_csa_type const &reverse_csa,
		block_graph const &gr,
		std::size_t i,
		std::size_t const end,
		bedinx_values_buffer &dst
	)
	{
		bedinx_set_positions_for_range(csa, reverse_csa, gr, u_row_size <t_u_block_size>(gr), i, end, dst);
	}
	
	
	void alr_values_for_range(
		csa_type const &csa,
		reverse_csa_type const &reverse_csa,
		block_graph const &gr,
		rank_support_type <path_index_support_base::d_bit_vector_type, 1> const &d_rank1_support,
		std::size_t i,
		std::size_t const end,
		alr_values_buffer &dst
	);
}

#endif
