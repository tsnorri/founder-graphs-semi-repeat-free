/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_CST_HH
#define FOUNDER_GRAPHS_CST_HH

#include <limits>
#include <sdsl/cst_sct3.hpp>


namespace founder_graphs {
	
	typedef sdsl::csa_wt <>	csa_type;
	
	typedef sdsl::cst_sct3 <
		csa_type,
		sdsl::lcp_dac <>,
		sdsl::bp_support_sada <>,
		sdsl::bit_vector,
		sdsl::rank_support_v5 <>,
		sdsl::select_support_mcl <>
	> cst_type;
	
	typedef decltype(std::declval <cst_type::node_type>().i) cst_interval_endpoint_type;
	constexpr inline auto CST_INTERVAL_ENDPOINT_MAX{std::numeric_limits <cst_interval_endpoint_type>::max()};
}

#endif
