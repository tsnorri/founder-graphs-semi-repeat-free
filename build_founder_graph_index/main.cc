/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cereal/archives/portable_binary.hpp>
#include <founder_graphs/basic_types.hh>
#include <founder_graphs/msa_reader.hh>
#include <iostream>
#include <libbio/file_handling.hh>
#include <libbio/utility.hh>
#include <set>
#include <string>
#include "cmdline.h"

namespace fg	= founder_graphs;
namespace lb	= libbio;


namespace {
	
	typedef std::set <std::string, lb::compare_strings_transparent>	segment_set;
	typedef std::multiset <std::string>								segment_buffer_type;
	
	
	// For assigning std::span <char> to std::string.
	void assign(std::span <char> const src, std::string &dst)
	{
		dst.resize(src.size());
		std::copy(src.begin(), src.end(), dst.begin());
	}
	
	
	void handle_block_range(
		fg::msa_reader &reader,
		std::size_t const lb,
		std::size_t const rb,
		char const separator,
		segment_set &concatenated_segments,
		segment_buffer_type &segment_buffer
	)
	{
		// Move the nodes to the buffer.
		while (!concatenated_segments.empty())
		{
			auto nh(concatenated_segments.extract(concatenated_segments.begin()));
			nh.value().clear();
			segment_buffer.insert(segment_buffer.end(), std::move(nh));
		}
		
		// Currently fill_buffer() returns after the callback has finished.
		reader.fill_buffer(lb, rb, [&concatenated_segments, &segment_buffer](fg::msa_reader::span_vector const &spans){
			for (auto const &span : spans)
			{
				// Check if this segment has already been handled.
				auto const it(concatenated_segments.find(span));
				if (concatenated_segments.end() == it)
				{
					// New segment, add to the set.
					if (segment_buffer.empty())
						concatenated_segments.emplace(span.data(), span.size());
					else
					{
						auto nh(segment_buffer.extract(segment_buffer.begin()));
						assign(span, nh.value());
						concatenated_segments.insert(std::move(nh));
					}
				}
			}
			
			return true;
		});
		
		// Output.
		for (auto const &segment : concatenated_segments)
			std::cout << separator << segment;
	}
	
	
	void generate_indexable_text(
		fg::msa_reader &reader,
		char const *sequence_list_path,
		char const *segmentation_path,
		bool const input_is_gzipped,
		bool const should_output_segments_only
	)
	{
		// Open the segmentation.
		lb::file_istream segmentation_stream;
		lb::open_file_for_reading(segmentation_path, segmentation_stream);
		cereal::PortableBinaryInputArchive archive(segmentation_stream);
		
		// Read the sequence file paths.
		{
			lb::file_istream sequence_list_stream;;
			lb::open_file_for_reading(sequence_list_path, sequence_list_stream);
			
			std::string path;
			while (std::getline(sequence_list_stream, path))
				reader.add_file(path);
		}
		
		reader.prepare();
		
		// Read the block boundaries and generate the text.
		// Maintain two ranges, [lb, mid) and [mid, rb).
		fg::length_type block_count{};
		archive(cereal::make_size_tag(block_count));
		
		segment_set concatenated_segments;
		segment_buffer_type segment_buffer;
		
		if (should_output_segments_only)
		{
			// Output segments one per line.
			fg::length_type lb{};
			for (fg::length_type i(0); i < block_count; ++i)
			{
				// Read the next right bound.
				fg::length_type rb{};
				archive(rb);
			
				handle_block_range(reader, lb, rb, '\n', concatenated_segments, segment_buffer);
			
				// Update the pointer.
				lb = rb;
			}
		}
		else
		{
			// Output a text that consists of concatenated pairs of segments separated by ‘#’ characters.
			fg::length_type lb{};
			if (block_count)
			{
				fg::length_type mid{};
				archive(mid);
				for (fg::length_type i(1); i < block_count; ++i)
				{
					// Read the next right bound.
					fg::length_type rb{};
					archive(rb);
				
					handle_block_range(reader, lb, rb, '#', concatenated_segments, segment_buffer);
				
					// Update the pointers.
					lb = mid;
					mid = rb;
				}
			}
		}
		
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
	
	if (args_info.generate_indexable_text_given)
	{
		if (args_info.bgzip_input_given)
		{
			fg::bgzip_msa_reader reader;
			generate_indexable_text(reader, args_info.sequence_list_arg, args_info.segmentation_arg, args_info.bgzip_input_given, args_info.segments_only_given);
		}
		else
		{
			fg::text_msa_reader reader;
			generate_indexable_text(reader, args_info.sequence_list_arg, args_info.segmentation_arg, args_info.bgzip_input_given, args_info.segments_only_given);
		}
	}
	else
	{
		std::cerr << "Unknown mode given.\n";
		return EXIT_FAILURE;
	}
	
	return EXIT_SUCCESS;
}
