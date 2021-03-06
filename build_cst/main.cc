/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */


#include <cereal/archives/portable_binary.hpp>
#include <founder_graphs/cst.hh>
#include <iostream>
#include <libbio/file_handling.hh>
#include "cmdline.h"

namespace lb = libbio;


namespace {

	void register_file_if_needed(sdsl::cache_config &config, char const *path, char const *key, char const *message)
	{
		if (path)
		{
			std::cerr << message << " path: " << path << '\n';
			config.file_map[key] = path;
		}
	}

	
	void build_cst(
		char const *input_path,
		char const *text_path,
		char const *sa_path,
		char const *bwt_path,
		char const *lcp_path,
		char const *csa_path,
		cereal::PortableBinaryOutputArchive &archive
	)
	{
		sdsl::cache_config config(false); // Do not remove temporary files automatically.
		register_file_if_needed(config, text_path, sdsl::conf::KEY_TEXT, "Text path");
		register_file_if_needed(config, sa_path, sdsl::conf::KEY_SA, "Suffix array");
		register_file_if_needed(config, bwt_path, sdsl::conf::KEY_BWT, "BWT");
		register_file_if_needed(config, lcp_path, sdsl::conf::KEY_LCP, "LCP");
		register_file_if_needed(config, csa_path, sdsl::conf::KEY_CSA, "CSA");

		founder_graphs::cst_type cst;
		sdsl::construct(cst, input_path, config, 1);
		archive(cst);
	}


	void write_cst(
		gengetopt_args_info const &args_info,
		cereal::PortableBinaryOutputArchive &archive
	)
	{
		build_cst(
			args_info.input_arg,
			args_info.text_arg,
			args_info.sa_arg,
			args_info.bwt_arg,
			args_info.lcp_arg,
			args_info.csa_arg,
			archive
		);
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

	if (args_info.output_arg)
	{
		lb::file_ostream stream;
		lb::open_file_for_writing(args_info.output_arg, stream, lb::writing_open_mode::CREATE);
		cereal::PortableBinaryOutputArchive archive(stream);
		write_cst(args_info, archive);
	}
	else
	{
		cereal::PortableBinaryOutputArchive archive(std::cout);
		write_cst(args_info, archive);
	}
	
	return EXIT_SUCCESS;
}
