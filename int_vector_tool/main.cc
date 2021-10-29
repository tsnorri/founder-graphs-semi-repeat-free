/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */


#include <iostream>
#include <sdsl/int_vector.hpp>
#include "cmdline.h"


int main(int argc, char **argv)
{
#ifndef NDEBUG
	std::cerr << "Assertions have been enabled." << std::endl;
#endif
	
	gengetopt_args_info args_info;
	if (0 != cmdline_parser(argc, argv, &args_info))
		std::exit(EXIT_FAILURE);
	
	std::ios_base::sync_with_stdio(false);	// Don't use C style IO after calling cmdline_parser.

	if (args_info.read_given)
	{
		sdsl::int_vector_size_type size{};
		std::uint8_t width{};
		auto const res(sdsl::int_vector <0>::read_header(size, width, std::cin));

		std::cerr << "Read " << res << " bytes.\n";
		std::cerr << "Size: " << size << '\n';
		std::cerr << "Width: " << int(width) << '\n';
		if (size)
		{
			std::cerr << "First 10 (or less) bytes:\n";
			for (std::size_t i(0); i < 10U; ++i)
			{
				std::uint8_t bb{};
				if (!(std::cin >> bb))
					break;
				std::cerr << std::dec << i << ": " << std::hex << int(bb) << '\n';
			}
		}
	}
	else if (args_info.write_given)
	{
		if (args_info.length_arg < 0)
		{
			std::cerr << "Length must be non-negative.\n";
			return EXIT_FAILURE;
		}

		auto const res(sdsl::int_vector <0>::write_header(args_info.length_arg, args_info.width_arg, std::cout));
		std::cerr << "Wrote " << res << " bytes.\n";
	}
	else
	{
		std::cerr << "Unknown mode.\n";
		return EXIT_FAILURE;
	}
	
	return EXIT_SUCCESS;
}
