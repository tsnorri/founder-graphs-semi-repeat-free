/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_BASIC_TYPES_HH
#define FOUNDER_GRAPHS_BASIC_TYPES_HH

#include <cstdint>
#include <limits>
#include <utility>	// std::pair


namespace founder_graphs {
	
	typedef std::uint32_t	count_type;
	typedef std::uint64_t	length_type;
	
	constexpr inline auto COUNT_MAX{std::numeric_limits <count_type>::max()};
	constexpr inline auto LENGTH_MAX{std::numeric_limits <length_type>::max()};
	
	template <typename t_item>
	using pair = std::pair <t_item, t_item>;
}

#endif
