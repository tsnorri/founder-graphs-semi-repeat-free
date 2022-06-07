/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_FOUNDER_GRAPH_INDICES_BASIC_TYPES_HH
#define FOUNDER_GRAPHS_FOUNDER_GRAPH_INDICES_BASIC_TYPES_HH

#include <founder_graphs/basic_types.hh>
#include <founder_graphs/lexicographic_range.hh>
#include <sdsl/csa_wt.hpp>
#include <sdsl/rrr_vector.hpp>
#include <sdsl/rank_support_v5.hpp>
#include <sdsl/select_support_mcl.hpp>


namespace founder_graphs::founder_graph_indices::detail {
	
	// Helpers for type aliases.
	template <typename t_bit_vector>
	struct rs_trait
	{
		template <std::uint8_t>
		struct rank_support {};
		
		template <std::uint8_t>
		struct select_support {};
		
		template <> struct rank_support <0> { typedef typename t_bit_vector::rank_0_type type; };
		template <> struct rank_support <1> { typedef typename t_bit_vector::rank_1_type type; };
		template <> struct select_support <0> { typedef typename t_bit_vector::select_0_type type; };
		template <> struct select_support <1> { typedef typename t_bit_vector::select_1_type type; };
		
		template <std::uint8_t t_i>
		using rank_support_type = typename rank_support <t_i>::type;
		
		template <std::uint8_t t_i>
		using select_support_type = typename select_support <t_i>::type;
	};
}


namespace founder_graphs::founder_graph_indices {
	
	template <typename t_bit_vector, std::uint8_t t_i>
	using rank_support_type = typename detail::rs_trait <t_bit_vector>::template rank_support_type <t_i>;
	
	template <typename t_bit_vector, std::uint8_t t_i>
	using select_support_type = typename detail::rs_trait <t_bit_vector>::template select_support_type <t_i>;
}


namespace founder_graphs::founder_graph_indices::detail {
	
	template <>
	struct rs_trait <sdsl::bit_vector>
	{
		template <std::uint8_t t_i>
		using rank_support_type = sdsl::rank_support_v5 <t_i>;
	
		template <std::uint8_t t_i>
		using select_support_type = sdsl::select_support_mcl <t_i>;
	};
	
	
	// Common interface for the Wavelet trees (s.t. we can get a type for a template template parameter).
	template <typename t_bv, typename t_rank1, typename t_select1, typename t_select0>
	using wt_huff_t = sdsl::wt_huff <t_bv, t_rank1, t_select1, t_select0>;
	
	template <typename t_bv, typename t_rank1, typename t_select1, typename t_select0>
	using wt_hutu_t = sdsl::wt_hutu <t_bv, t_rank1, t_select1, t_select0>;
	
	
	template <
		template <typename, typename, typename, typename> typename t_wt,
		typename t_tag,
		typename t_bv = typename t_tag::bit_vector_type
	>
	using wt_t = t_wt <
		t_bv,
		rank_support_type <t_bv, 1>,
		select_support_type <t_bv, 1>,
		select_support_type <t_bv, 0>
	>;
}


namespace founder_graphs::founder_graph_indices {
	
	struct uncompressed_tag
	{
		typedef sdsl::bit_vector	bit_vector_type;
	};
	
	template <std::uint8_t t_block_size>
	struct rrr_compressed_tag
	{
		constexpr static inline std::uint8_t	block_size{t_block_size};
		typedef sdsl::rrr_vector <block_size>	bit_vector_type;
	};
	
	
	template <typename t_tag>
	using wt_int_t = detail::wt_t <sdsl::wt_int, t_tag>;
	
	template <typename t_tag>
	using wt_huff_t = detail::wt_t <detail::wt_huff_t, t_tag>;
	
	template <typename t_tag>
	using wt_hutu_t = detail::wt_t <detail::wt_hutu_t, t_tag>;
	
	
	template <typename t_wt>
	using csa_t = sdsl::csa_wt <
		t_wt,
		1 << 30,												// Sample density for suffix array (SA) values
		1 << 30,												// Sample density for inverse suffix array (ISA) values
		sdsl::sa_order_sa_sampling <>,							// Default
		sdsl::isa_sampling <>,									// Default
		sdsl::alphabet_trait <sdsl::byte_alphabet_tag>::type	// Make the CSA use a byte alphabet instead of the integer alphabet as suggested by the WT.
	>;
	
	
	//typedef csa_t <wt_int_t <rrr_compressed_tag <15>>>	csa_type;
	//typedef csa_t <wt_huff_t <rrr_compressed_tag <15>>>	reverse_csa_type;
	typedef csa_t <wt_huff_t <rrr_compressed_tag <15>>>	csa_type;
	typedef csa_type reverse_csa_type;
	
	
	static_assert(std::is_same_v <csa_type::size_type, reverse_csa_type::size_type>);
	constexpr inline auto CSA_SIZE_MAX{std::numeric_limits <csa_type::size_type>::max()};
	
	
	typedef founder_graphs::lexicographic_range <csa_type>							lexicographic_range;
	typedef founder_graphs::lexicographic_range <reverse_csa_type>					co_lexicographic_range;
	typedef founder_graphs::lexicographic_range_pair <csa_type, reverse_csa_type>	lexicographic_range_pair;
}

#endif
