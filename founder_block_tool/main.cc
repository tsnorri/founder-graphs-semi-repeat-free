/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cereal/archives/portable_binary.hpp>
#include <founder_graphs/basic_types.hh>
#include <iostream>
#include <map>
#include <range/v3/view/reverse.hpp>
#include "cmdline.h"


namespace fg	= founder_graphs;
namespace rsv	= ranges::views;


int main(int argc, char **argv)
{
#ifndef NDEBUG
	std::cerr << "Assertions have been enabled." << std::endl;
#endif
	
	gengetopt_args_info args_info;
	if (0 != cmdline_parser(argc, argv, &args_info))
		std::exit(EXIT_FAILURE);
	
	std::ios_base::sync_with_stdio(false);	// Don't use C style IO after calling cmdline_parser.

	// Read the size of the input.
	cereal::PortableBinaryInputArchive archive(std::cin);

	std::size_t aligned_size{}; // FIXME: use fg::length_type, also change the typedef to std::uint64_t.
	archive(cereal::make_size_tag(aligned_size));

	if (args_info.read_given)
	{
		std::cout << "LB\tRB\n";
		for (std::size_t i(0); i < aligned_size; ++i)
		{
			auto const aln_pos(aligned_size - i  - 1);
			fg::length_type rb{};
			archive(rb);
			std::cout << aln_pos << '\t' << rb << '\n';
		}
	}
	else if (args_info.statistics_given)
	{
		std::map <fg::length_type, fg::length_type> histogram;
		fg::length_type length{};
		for (std::size_t i(0); i < aligned_size; ++i)
		{
			auto const aln_pos(aligned_size - i - 1);

			fg::length_type rb{};
			archive(rb);
			if (fg::LENGTH_MAX == rb)
				length = rb;
			else
				length = rb - aln_pos + 1;

			++histogram[length];
		}

		if (histogram.empty())
		{
			std::cerr << "There were no blocks in the input.\n";
			return EXIT_FAILURE;
		}

		std::cerr << "The first block is " << (fg::LENGTH_MAX == length ? "not " : "") << "semi-repeat-free.\n";
		std::cerr << "Minimum length: " << histogram.begin()->first << '\n';
		for (auto const &kv : rsv::reverse(histogram))
		{
			if (fg::LENGTH_MAX != kv.first)
			{
				std::cerr << "Maximum length: " << kv.first << '\n';
				break;
			}
		}

		{
			// Mean and median.
			std::uint64_t length_sum{};
			std::uint64_t count_sum{};
			for (auto const &kv : histogram)
			{
				if (fg::LENGTH_MAX == kv.first)
					continue;

				length_sum += kv.second * kv.first;
				count_sum += kv.second;
			}

			std::cerr << "Mean length: " << (double(length_sum) / count_sum) << '\n';

			// Median.
			auto const count_2(count_sum / 2);
			std::uint64_t current_count{};
			for (auto const &kv : histogram)
			{
				if (fg::LENGTH_MAX == kv.first)
					continue;

				if (current_count + kv.second <= count_2)
				{
					std::cerr << "Median length: " << kv.first << '\n';
					break;
				}

				current_count += kv.second;
			}
		}

		std::cout << "LENGTH\tCOUNT\n";
		for (auto const &kv : histogram)
			std::cout << kv.first << '\t' << kv.second << '\n';
	}
	else
	{
		std::cerr << "Unknown mode.\n";
		return EXIT_FAILURE;
	}
	
	return EXIT_SUCCESS;
}
