/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cereal/archives/portable_binary.hpp>
#include <compare>
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
	
	// Compare a substring that originates from the input sequences (rhs) to one that has already
	// been stored (lhs). We require that the stored sequences may not contain gap characters.
	// To check for prefixes later, we (unfortunately) need lexicographic order instead of
	// e.g. first ordering by length.
	struct segment_cmp
	{
		typedef std::true_type is_transparent;
		
		// FIXME: I don’t know how a comparison operator that returns std::strong_ordering is supposed to be named.
		template <typename t_char, std::size_t t_extent>
		std::strong_ordering strong_order(std::string const &lhs, std::span <t_char, t_extent> const rhs) const
		{
			std::size_t i(0);
			std::size_t j(0);
			std::size_t lhs_gap_count(0);
			std::size_t const lc(lhs.size());
			std::size_t const rc(rhs.size());
			while (i < lc && j < rc)
			{
				// Check for a gap.
				while ('-' == rhs[j])
				{
					++j;
					++lhs_gap_count;
					if (j == rc)
						goto exit_loop;
				}
				
				// Compare and check for equality.
				auto const res(lhs[i] <=> rhs[j]);
				if (std::is_neq(res))
					return res;
				
				// Continue.
				++i;
				++j;
			}
			
		exit_loop:
			// The strings have equal prefixes. Compare the lengths.
			return ((lc - lhs_gap_count) <=> rc);
		}
		
		// Less-than operators.
		template <typename t_char, std::size_t t_extent>
		bool operator()(std::string const &lhs, std::span <t_char, t_extent> const rhs) const
		{
			return std::is_lt(strong_order(lhs, rhs));
		}
		
		template <typename t_char, std::size_t t_extent>
		bool operator()(std::span <t_char, t_extent> const lhs, std::string const &rhs) const
		{
			return std::is_gteq(strong_order(rhs, lhs));
		}
		
		// For pairs of map keys.
		bool operator()(std::string const &lhs, std::string const &rhs) const
		{
			return lhs < rhs;
		}
	};
	
	
	typedef std::map <std::string, std::size_t, segment_cmp>		segment_map;
	typedef std::multimap <std::string, std::size_t, segment_cmp>	segment_buffer_type;
	
	
	// For assigning std::span <char> to std::string.
	void remove_gaps_and_assign(std::span <char> const src, std::string &dst)
	{
		dst.resize(src.size());
		std::copy_if(src.begin(), src.end(), dst.begin(), [](auto const cc){
			return '-' != cc;
		});
	}
	
	
	void handle_block_range(
		fg::msa_reader &reader,
		std::size_t const lb,
		std::size_t const rb,
		segment_map &concatenated_segments,
		segment_buffer_type &segment_buffer,
		std::size_t const segment_idx, 			// Segment or segment pair index.
		bool const should_count_prefixes,
		bool const should_omit_segments
	)
	{
		// Move the nodes to the buffer.
		while (!concatenated_segments.empty())
		{
			auto nh(concatenated_segments.extract(concatenated_segments.begin()));
			nh.key().clear();
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
					{
						std::string key;
						remove_gaps_and_assign(span, key);
						concatenated_segments.emplace(std::move(key), 0);
					}
					else
					{
						auto nh(segment_buffer.extract(segment_buffer.begin()));
						remove_gaps_and_assign(span, nh.key());
						nh.mapped() = 0;
						concatenated_segments.insert(std::move(nh));
					}
				}
			}
			
			return true;
		});
		
		if (should_count_prefixes)
		{
			// Count the prefixes and output.
			// Use a naïve algorithm since the amount of data is expected to be small,
			// i.e. just check if a segment is a prefix of the succeeding ones in the lexicographic order.
			auto it(concatenated_segments.begin());
			auto const end(concatenated_segments.end());
			while (it != end)
			{
				libbio_assert_eq(0, it->second);
				auto next_it(std::next(it));
				while (next_it != end && next_it->first.starts_with(it->first))
				{
					++it->second;
					++next_it;
				}
				++it;
			}
			
			if (should_omit_segments)
			{
				for (auto const &kv : concatenated_segments)
					std::cout << segment_idx << '\t' << kv.second << '\t' << kv.first.size() << '\n';
			}
			else
			{
				for (auto const &kv : concatenated_segments)
					std::cout << segment_idx << '\t' << kv.second << '\t' << kv.first << '\n';
			}
		}
		else
		{
			// Output.
			for (auto const &kv : concatenated_segments)
				std::cout << '#' << kv.first;
		}
	}
	
	
	void generate_indexable_text(
		fg::msa_reader &reader,
		char const *sequence_list_path,
		char const *segmentation_path,
		bool const input_is_gzipped,
		bool const should_output_block_contents_only,
		bool const should_omit_segments
	)
	{
		// Open the segmentation.
		lb::log_time(std::cerr) << "Loading the segmentation…\n";
		lb::file_istream segmentation_stream;
		lb::open_file_for_reading(segmentation_path, segmentation_stream);
		cereal::PortableBinaryInputArchive archive(segmentation_stream);
		
		// Read the sequence file paths.
		{
			lb::log_time(std::cerr) << "Loading the sequences…\n";
			lb::file_istream sequence_list_stream;;
			lb::open_file_for_reading(sequence_list_path, sequence_list_stream);
			
			std::string path;
			while (std::getline(sequence_list_stream, path))
				reader.add_file(path);
		}
		
		reader.prepare();
		
		// Read the block boundaries and generate the text.
		lb::log_time(std::cerr) << "Processing…\n";
		fg::length_type block_count{};
		archive(cereal::make_size_tag(block_count));
		
		segment_map concatenated_segments;
		segment_buffer_type segment_buffer;
		
		if (should_output_block_contents_only)
		{
			// Output segments one per line with prefix counts.
			if (should_omit_segments)
				std::cout << "SEGMENT_INDEX\tPREFIX_COUNT\tLABEL_LENGTH\n";
			else
				std::cout << "SEGMENT_INDEX\tPREFIX_COUNT\tLABEL\n";
			fg::length_type lb{};
			for (fg::length_type i(0); i < block_count; ++i)
			{
				// Read the next right bound.
				fg::length_type rb{};
				archive(rb);
			
				lb::log_time(std::cerr) << "Block " << (1 + i) << '/' << block_count << "…\n";
				handle_block_range(reader, lb, rb, concatenated_segments, segment_buffer, i, true, should_omit_segments);
			
				// Update the pointer.
				lb = rb;
			}
		}
		else
		{
			// Output a text that consists of concatenated pairs of segments separated by ‘#’ characters.
			// Maintain two ranges, [lb, mid) and [mid, rb).
			fg::length_type lb{};
			if (block_count)
			{
				fg::length_type mid{};
				archive(mid);
				for (fg::length_type i(1); i < block_count; ++i)
				{
					lb::log_time(std::cerr) << "Block " << i << '/' << (block_count - 1) << "…\n";

					// Read the next right bound.
					fg::length_type rb{};
					archive(rb);
				
					handle_block_range(reader, lb, rb, concatenated_segments, segment_buffer, i - 1, false, false);
				
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
			generate_indexable_text(reader, args_info.sequence_list_arg, args_info.segmentation_arg, args_info.bgzip_input_given, args_info.block_contents_given, args_info.omit_segments_given);
		}
		else
		{
			fg::text_msa_reader reader;
			generate_indexable_text(reader, args_info.sequence_list_arg, args_info.segmentation_arg, args_info.bgzip_input_given, args_info.block_contents_given, args_info.omit_segments_given);
		}
	}
	else
	{
		std::cerr << "Unknown mode given.\n";
		return EXIT_FAILURE;
	}
	
	return EXIT_SUCCESS;
}
