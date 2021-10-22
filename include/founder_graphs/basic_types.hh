/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_BASIC_TYPES_HH
#define FOUNDER_GRAPHS_BASIC_TYPES_HH

#include <cstddef>
#include <limits>


namespace founder_graphs {
	
	typedef std::size_t	length_type;
	constexpr inline auto LENGTH_MAX{std::numeric_limits <length_type>::max()};
}

#endif
