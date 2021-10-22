/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cstdio> // strerror
#include <founder_graphs/reverse_msa_reader.hh>
#include <libbio/assert.hh>
#include <libbio/file_handling.hh>
#include <range/v3/view/enumerate.hpp>
#include <stdexcept> // std::runtime_error
#include <sys/stat.h> // fstat
#include <sys/types.h> // pread
#include <sys/uio.h> // pread
#include <tuple>
#include <unistd.h> // pread

namespace lb	= libbio;
namespace rsv	= ranges::views;


namespace {
	
	std::tuple <std::size_t, std::size_t> check_sizes(lb::file_handle const &handle)
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


namespace founder_graphs {
	
	void reverse_msa_reader::add_file(std::string const &path)
	{
		bool const is_empty(m_handles.empty());
		auto const &handle(m_handles.emplace_back(lb::open_file_for_reading(path)));
		
		auto const [aln_size, preferred_block_size] = check_sizes(handle);
		if (is_empty)
		{
			m_aln_size = aln_size;
			m_preferred_block_size = preferred_block_size;
		}
		else
		{
			libbio_always_assert_eq(m_aln_size, aln_size);
			libbio_always_assert_eq(m_preferred_block_size, preferred_block_size);
		}
	}
	
	
	void reverse_msa_reader::prepare()
	{
		m_buffer.resize(m_handles.size() * m_preferred_block_size, 0);
		m_file_position = m_aln_size;
	}
	
	
	bool reverse_msa_reader::fill_buffer()
	{
		if (0 == m_file_position)
			return false;
		
		auto const read_count(std::min(m_file_position, m_preferred_block_size));
		m_file_position -= read_count;
		for (auto const &[i, handle] : rsv::enumerate(m_handles))
			read_from_file(handle, m_file_position, read_count, m_buffer.data() + i * m_preferred_block_size);
		
		return true;
	}
}
