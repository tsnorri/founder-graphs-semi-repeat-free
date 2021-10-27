/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

// Make sure SDSL’s serialization functions are used.
#define CEREAL_LOAD_FUNCTION_NAME cereal_load
#define CEREAL_SAVE_FUNCTION_NAME cereal_save

#include <divsufsort.h>
#include <founder_graphs/utility.hh>
#include <libbio/assert.hh>
#include <libbio/file_handle.hh>
#include <libbio/file_handling.hh>
#include <iostream>
#include <sdsl/int_vector.hpp>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "cmdline.h"

namespace fg	= founder_graphs;
namespace lb	= libbio;


namespace {
	
	typedef std::vector <std::uint8_t> input_vector;
	
	
	template <std::size_t t_int_width, std::size_t t_shift_amt>
	void resize_if_needed(sdsl::int_vector <0> &sa, std::size_t const file_size)
	{
		// Use only as many bits per value as needed.
		auto const new_width(sdsl::bits::hi(file_size) + 1);
		if (new_width < t_int_width)
		{
			for (std::size_t i(0); i < file_size; ++i)
				sa.set_int(i * new_width, sa.get_int(i << t_shift_amt, t_int_width), new_width);
			
			sa.width(new_width);
			sa.resize(file_size);
		}
	}
	
	
	void read_file(lb::file_handle const &handle, input_vector &buffer, std::size_t const preferred_block_size)
	{
		// Try to reduce the number of system calls needed by using ::readv.
		std::array <struct iovec, 32> iov;
		
		auto const fd(handle.get());
		std::size_t pos(0);
		auto *buffer_start(buffer.data());
		while (pos < buffer.size())
		{
			for (std::size_t i(0); i < iov.size(); ++i)
			{
				auto &iovv(iov[i]);
				iovv.iov_base = buffer_start + pos + i * preferred_block_size;
				iovv.iov_len = preferred_block_size; 
			}
			
			auto const res(::readv(fd, iov.data(), iov.size()));
			switch (res)
			{
				case -1:
					throw std::runtime_error(std::strerror(errno));
				case 0:
					libbio_always_assert_eq(pos, buffer.size());
					break;
				default:
					pos += res;
					break;
			}
		}
	}
	
	
	void read_sa(char const *input_path)
	{
		lb::file_istream stream;
		lb::open_file_for_reading(input_path, stream);
		
		sdsl::int_vector <0> sa;
		sa.load(stream);
		
		auto const size(sa.size());
		std::cout << "Elements: " << size << '\n';
		std::cout << "Width:    " << int(sa.width()) << '\n';
		
		for (std::size_t i(0); i < size; ++i)
			std::cout << i << ":\t" << sa[i] << '\n';
	}
	
	
	void build_sa(char const *input_path)
	{
		lb::file_handle handle(lb::open_file_for_reading(input_path));
		libbio_always_assert_neq(-1, handle.get());
		
		// Read the file into memory.
		auto const [file_size, preferred_block_size] = fg::check_file_size(handle);
		input_vector input(file_size, 0);
		read_file(handle, input, preferred_block_size);
		
		// divsufsort puts everything in the global namespace.
		sdsl::int_vector <0> sa;
		if (0xffffffffU < file_size)
		{
			sa.width(32);
			sa.resize(file_size);
			auto const res(::divsufsort(input.data(), reinterpret_cast <std::int32_t *>(sa.data()), file_size));
			libbio_always_assert_eq(0, res);
			
			// FIXME: I don’t understand why SDSL uses i << 5 here.
			resize_if_needed <32, 5>(sa, file_size);
		}
		else
		{
			sa.width(64);
			sa.resize(file_size);
			auto const res(::divsufsort(input.data(), reinterpret_cast <std::int64_t *>(sa.data()), file_size));
			libbio_always_assert_eq(0, res);
			
			resize_if_needed <64, 6>(sa, file_size);
		}
		
		sa.serialize(std::cout);
		std::cout << std::flush;
	}
}


int main(int argc, char **argv)
{
#ifndef NDEBUG
	std::cerr << "Assertions have been enabled." << std::endl;
#endif
	
	gengetopt_args_info args_info;
	if (0 != cmdline_parser(argc, argv, &args_info))
		std::exit(EXIT_FAILURE);
	
	std::ios_base::sync_with_stdio(false);	// Don't use C style IO after calling cmdline_parser.
	std::cin.tie(nullptr);					// We don't require any input from the user.
	
	if (args_info.read_sa_flag)
		read_sa(args_info.input_arg);
	else
		build_sa(args_info.input_arg);
	
	return EXIT_SUCCESS;
}
