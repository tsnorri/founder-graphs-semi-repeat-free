/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cstdio>
#include <libbio/assert.hh>
#include <libbio/file_handling.hh>
#include <string>
#include <vector>
#include "cmdline.h"

namespace lb	= libbio;


namespace {
	
	constexpr static inline std::size_t BUFFER_SIZE{16384};
	
	
	void remove_ranges(std::istream &stream, char const *range_list_path, std::size_t const padding)
	{
		std::vector <std::pair <std::size_t, std::size_t>> ranges;
		
		{
			// Read the ranges and sort.
			lb::file_istream range_stream;
			lb::open_file_for_reading(range_list_path, range_stream);
			
			std::size_t lb{};
			std::size_t rb{};
			while (range_stream >> lb >> rb)
			{
				libbio_always_assert_lt(lb, rb);
				if (rb - lb < 2 * padding)
					continue;

				ranges.emplace_back(lb + padding, rb - padding);
			}
			
			// Add a sentinel.
			ranges.emplace_back(SIZE_MAX, SIZE_MAX);
			
			std::sort(ranges.begin(), ranges.end());
		}
		
		try
		{
			// Read the input and process.
			auto range_it(ranges.begin());
			std::vector <char> buffer(BUFFER_SIZE, 0);
			std::size_t pos{};
			
			while (true)
			{
				stream.read(buffer.data(), BUFFER_SIZE);
				auto const read_size(stream.gcount());
				
				std::size_t buffer_pos{};
				while (true)
				{
					// Handle the bytes before the range.
					auto const size_before(std::min(read_size - buffer_pos, range_it->first - pos));
					std::cout.write(buffer.data() + buffer_pos, size_before);
					pos += size_before;
					buffer_pos += size_before;

					if (buffer_pos == read_size)
						break;
					
					// Handle the range.
					auto const skipped_length(std::min(read_size - buffer_pos, range_it->second - range_it->first));
					range_it->first += skipped_length;
					pos += skipped_length;
					buffer_pos += skipped_length;
					
					if (range_it->first == range_it->second)
						++range_it;
					
					if (0 == skipped_length)
						break;
				}
				
				if (!stream)
					break;
			}
		}
		catch (std::exception const &exc)
		{
			std::cerr << "Got an error: " << exc.what() << '\n';
			throw exc;
		}
		
		std::cout.flush();
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
	std::cout.exceptions(std::cout.exceptions() | std::ios::badbit | std::ios::failbit);

	if (args_info.padding_arg < 0)
	{
		std::cerr << "Padding must be non-negative.\n";
		std::exit(EXIT_FAILURE);
	}
	
	if (args_info.input_arg)
	{
		lb::file_istream stream;
		lb::open_file_for_reading(args_info.input_arg, stream);
		remove_ranges(stream, args_info.range_list_arg, args_info.padding_arg);
	}
	else
	{
		remove_ranges(std::cin, args_info.range_list_arg, args_info.padding_arg);
	}
	
	return EXIT_SUCCESS;
}
