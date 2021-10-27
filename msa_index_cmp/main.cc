/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */


#include <cereal/archives/portable_binary.hpp>
#include <founder_graphs/msa_index.hh>
#include <iostream>
#include <libbio/assert.hh>
#include <libbio/file_handling.hh>
#include "cmdline.h"

namespace fg	= founder_graphs;
namespace lb	= libbio;


namespace {
	
	void read_index(char const *path, fg::msa_index &index)
	{
		lb::file_istream in;
		lb::open_file_for_reading(path, in);
		cereal::PortableBinaryInputArchive archive(in);
		archive(index);
	}
	
	
	int msa_index_cmp(char const *lhs_path, char const *rhs_path)
	{
		fg::msa_index lhs;
		fg::msa_index rhs;
		
		read_index(lhs_path, lhs);
		read_index(rhs_path, rhs);
		
		std::cerr << "Indices match: " << int(lhs == rhs) << ".\n";
		
		auto const lhs_size(lhs.sequence_indices.size());
		auto const rhs_size(rhs.sequence_indices.size());
		std::cerr << "Entries: " << lhs_size << " (lhs), " << rhs_size << " (rhs).\n";
		if (lhs_size == rhs_size)
		{
			for (std::size_t i(0); i < lhs_size; ++i)
			{
				auto const &lhs_seq_idx(lhs.sequence_indices[i]);
				auto const &rhs_seq_idx(rhs.sequence_indices[i]);
				if (lhs_seq_idx != rhs_seq_idx)
				{
					std::cerr << "Entries at index " << i << " differ.\n";
					
					auto const &lhs_gp(lhs_seq_idx.gap_positions);
					auto const &rhs_gp(rhs_seq_idx.gap_positions);
					int const gap_positions_res(lhs_gp == rhs_gp);
					std::cerr << "\tGap positions: " << gap_positions_res << ".\n";
					if (!gap_positions_res)
					{
						auto const lhs_gp_size(lhs_gp.size());
						auto const rhs_gp_size(rhs_gp.size());
						std::cerr << "\t\tsize: " << lhs_gp_size << " (lhs), " << rhs_gp_size << " (rhs).\n";
						if (lhs_gp_size == rhs_gp_size)
						{
							for (std::size_t j(0); j < lhs_gp_size; ++j)
							{
								if (lhs_gp[j] != rhs_gp[j])
									std::cerr << "\t\tPosition " << j << ": " << lhs_gp[j] << " (lhs), " << rhs_gp[j] << " (rhs).\n";
							}
						}
					}
					
					std::cerr << "\trank_0: " << int(lhs_seq_idx.rank0_support == rhs_seq_idx.rank0_support) << ".\n";
					std::cerr << "\tselect_0: " << int(lhs_seq_idx.select0_support == rhs_seq_idx.select0_support) << ".\n";
				}
			}
		}
		
		return (!(lhs == rhs));
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
	
	return msa_index_cmp(args_info.lhs_arg, args_info.rhs_arg);
}
