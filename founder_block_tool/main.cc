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


namespace {

	typedef std::map <fg::length_type, fg::length_type>	length_map;


	void output_histogram(length_map const &histogram)
	{
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

				if (count_2 <= current_count + kv.second)
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


	void handle_first_stage_segmentation(gengetopt_args_info const &args_info)
	{
		// Read the size of the input.
		cereal::PortableBinaryInputArchive archive(std::cin);
		fg::length_type aligned_size{};
		archive(cereal::make_size_tag(aligned_size));

		if (args_info.read_given)
		{
			std::cout << "LB\tRB\n";
			for (fg::length_type i(0); i < aligned_size; ++i)
			{
				auto const aln_pos(aligned_size - i  - 1);
				fg::length_type rb{};
				archive(rb);
				std::cout << aln_pos << '\t' << rb << '\n';
			}
		}
		else if (args_info.right_bound_histogram_given)
		{
			length_map histogram;
			for (fg::length_type i(0); i < aligned_size; ++i)
			{
				fg::length_type rb{};
				archive(rb);
				++histogram[rb];
			}

			std::cout << "RB\tCOUNT\n";
			for (auto const &kv : histogram)
				std::cout << kv.first << '\t' << kv.second << '\n';
		}
		else if (args_info.length_histogram_given)
		{
			length_map histogram;
			fg::length_type length{};
			for (fg::length_type i(0); i < aligned_size; ++i)
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
				std::exit(EXIT_FAILURE);
			}

			std::cerr << "The first block is " << (fg::LENGTH_MAX == length ? "not " : "") << "semi-repeat-free.\n";
			output_histogram(histogram);
		}
		else
		{
			std::cerr << "Unknown mode.\n";
			std::exit(EXIT_FAILURE);
		}
	}


	void handle_optimized_segmentation(gengetopt_args_info const &args_info)
	{
		// Read the size of the input.
		cereal::PortableBinaryInputArchive archive(std::cin);
		fg::length_type count{}; // Block count.
		archive(cereal::make_size_tag(count));

		if (args_info.read_given)
		{
			std::cout << "LB\tRB\n";
			fg::length_type lb{};
			for (fg::length_type i(0); i < count; ++i)
			{
				fg::length_type rb{};
				archive(rb);
				std::cout << lb << '\t' << rb << '\n';
				lb = rb;
			}
		}
		else if (args_info.right_bound_histogram_given)
		{
			std::cerr << "Right bounds are all distinct in the optimized segmentation.\n";
			std::exit(EXIT_FAILURE);
		}
		else if (args_info.length_histogram_given)
		{
			length_map histogram;
			fg::length_type lb{};
			for (fg::length_type i(0); i < count; ++i)
			{
				fg::length_type rb{};
				archive(rb);
				auto const length(rb - lb);
				++histogram[length];
				lb = rb;
			}

			if (histogram.empty())
			{
				std::cerr << "There were no blocks in the input.\n";
				std::exit(EXIT_FAILURE);
			}

			output_histogram(histogram);
		}
		else
		{
			std::cerr << "Unknown mode.\n";
			std::exit(EXIT_FAILURE);
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

	if (args_info.optimized_segmentation_given)
		handle_optimized_segmentation(args_info);
	else
		handle_first_stage_segmentation(args_info);
	
	return EXIT_SUCCESS;
}
