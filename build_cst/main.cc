/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */


#include <cereal/archives/portable_binary.hpp>
#include <founder_graphs/cst.hh>
#include <iostream>
#include "cmdline.h"


namespace {
	
	void build_cst(char const *input_path)
	{
		founder_graphs::cst_type cst;
		sdsl::construct(cst, input_path, 1);
		
		// Output.
		cereal::PortableBinaryOutputArchive archive(std::cout);
		archive(cst);
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
	
	build_cst(args_info.input_arg);
	
	return EXIT_SUCCESS;
}
