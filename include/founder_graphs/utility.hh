/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_UTILITY_HH
#define FOUNDER_GRAPHS_UTILITY_HH

#include <cstddef>
#include <libbio/file_handle.hh>
#include <tuple>


namespace founder_graphs {
	
	std::tuple <std::size_t, std::size_t> check_file_size(libbio::file_handle const &handle);
	
	void read_from_file(libbio::file_handle const &handle, std::size_t const pos, std::size_t const read_count, char *buffer_start);
}

#endif
