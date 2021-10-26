/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cstddef>
#include <founder_graphs/utility.hh>
#include <libbio/assert.hh>
#include <stdexcept>
#include <sys/stat.h> // fstat
#include <sys/types.h> // pread
#include <sys/uio.h> // pread
#include <unistd.h> // pread

namespace lb = libbio;


namespace founder_graphs {
	
	std::tuple <std::size_t, std::size_t> check_file_size(lb::file_handle const &handle)
	{
		auto const fd(handle.get());
		struct stat sb{};
	
		if (-1 == fstat(fd, &sb))
			throw std::runtime_error(strerror(errno));
	
		if (sb.st_size < 0)
			return {0, 0};
	
		if (sb.st_blksize < 0) // Unsigned on both macOS and Linux but test anyway.
			return {0, 0};
	
		return {sb.st_size, sb.st_blksize};
	}
	
	
	void read_from_file(lb::file_handle const &handle, std::size_t const pos, std::size_t const read_count, char *buffer_start)
	{
		auto const res(::pread(handle.get(), buffer_start, read_count, pos));
		if (-1 == res)
			throw std::runtime_error(strerror(errno));
		
		libbio_always_assert_eq(read_count, res);
	}
}
