/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cereal/archives/portable_binary.hpp>
#include <founder_graphs/founder_graph_indices/basic_types.hh>
#include <founder_graphs/founder_graph_indices/path_index.hh>
#include <iostream>
#include <libbio/assert.hh>
#include <libbio/file_handling.hh>
#include <range/v3/algorithm/copy.hpp>				// ranges::copy()
#include <range/v3/iterator/stream_iterators.hpp>	// ranges::make_ostream_joiner()
#include <sys/resource.h>							// getrusage()
#include "cmdline.h"

namespace ch	= std::chrono;
namespace fg	= founder_graphs;
namespace fgi	= founder_graphs::founder_graph_indices;
namespace lb	= libbio;


namespace {
	
	void process(char const *index_path, std::istream &ps, bool const use_prompt)
	{
		fgi::path_index index;
		
		{
			std::cerr << "Loading the index from " << index_path << "…" << std::flush;
			lb::file_istream stream;
			lb::open_file_for_reading(index_path, stream);
			cereal::PortableBinaryInputArchive iarchive(stream);
			iarchive(index);
			std::cerr << " Done.\n";
		}
		
		// Preallocate memory for the occurrences.
		std::vector <fg::count_type> path_matches;
		sdsl::bit_vector occ_buffer;
		sdsl::bit_vector head_buffer;
		sdsl::bit_vector tail_buffer;
		
		auto const input_count(index.get_input_count());
		path_matches.reserve(input_count);
		occ_buffer.reserve(input_count);
		head_buffer.reserve(input_count);
		tail_buffer.reserve(input_count);
		
		fg::length_type block_aln_pos{};
		fg::length_type offset{};
		
		{
			std::string buffer;
			struct rusage usage{};
			
			std::cout << "MATCH_COUNT\tDID_EXPAND\tWALL_TIME\tUSER_TIME\tBLOCK_ALN_POS\tMATCH_OFFSET\tMATCHES\n";
			
			while (true)
			{
				if (use_prompt)
					std::cerr << "Pattern? " << std::flush;
				
				ps >> buffer;
				if (ps.eof())
				{
					std::cout << std::flush;
					std::exit(EXIT_SUCCESS);
				}
				
				path_matches.clear();
				
				getrusage(RUSAGE_SELF, &usage);
				auto const t1_user(usage.ru_utime);
				auto const t1_wall_clock(ch::steady_clock::now());
				auto const res(index.list_occurrences(
					buffer.begin(),
					buffer.end(),
					block_aln_pos,
					offset,
					std::back_inserter(path_matches),
					occ_buffer,
					head_buffer,
					tail_buffer
				));
				auto const t2_wall_clock(ch::steady_clock::now());
				getrusage(RUSAGE_SELF, &usage);
				auto const t2_user(usage.ru_utime);
				
				ch::duration <double, std::micro> const wall_clock_diff_us(t2_wall_clock - t1_wall_clock);
				std::uint64_t const user_time_diff_us(std::uint64_t(t2_user.tv_sec - t1_user.tv_sec) * 1000000U + (t2_user.tv_usec - t1_user.tv_usec));
				
				auto const [match_count, did_expand] = res;
				
				std::cout
					<< match_count << '\t'
					<< (+did_expand) << '\t'
					<< wall_clock_diff_us.count() << '\t'
					<< user_time_diff_us << '\t';
				
				if (did_expand)
				{
					std::cout
						<< block_aln_pos << '\t'
						<< offset << '\t';
					ranges::copy(path_matches, ranges::make_ostream_joiner(std::cout, ","));
					std::cout << '\n';
				}
				else
				{
					std::cout << "0\t0\t\n";
				}
			}
		}
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
	
	if (args_info.pattern_input_arg)
	{
		std::cerr << "Reading patterns from " << args_info.pattern_input_arg << "…\n";
		lb::file_istream pattern_stream;
		lb::open_file_for_reading(args_info.pattern_input_arg, pattern_stream);
		
		process(args_info.index_input_arg, pattern_stream, false);
	}
	else
	{
		process(args_info.index_input_arg, std::cin, !args_info.without_prompt_flag);
	}
	
	return EXIT_SUCCESS;
}
