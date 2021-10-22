/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */


#include <cereal/archives/portable_binary.hpp>
#include <founder-graphs/msa_index.hh>
#include <iostream>
#include <libbio/assert.hh>
#include <libbio/file_handling.hh>
#include <set>
#include <vector>
#include "cmdline.h"

namespace lb	= libbio;


namespace {
	
	struct dp_state
	{
		std::size_t score{SIZE_MAX};
		std::size_t pos{SIZE_MAX};
		
		dp_state() = default;
		
		dp_state(std::size_t const score_, std::size_t const pos_):
			score(score_),
			pos(pos_)
		{
		}
	};
	
	
	void optimize_segmentation(char const *sequence_list_path)
	{
		lb::open_file_for_reading(sequence_list_path, sequence_path_stream);
		
		reverse_msa_reader reader;
		{
			std::string line;
			while (std::getline(sequence_path_stream, line))
				reader.add_file(line);
		}
		
		// Prepare the reader.
		reader.prepare();
		auto const seq_count(reader.handle_count());
		auto const block_size(reader.block_size());
		auto const aligned_size(reader.aligned_size());
		
		if (0 == seq_count)
			return;
		
		// Handle the segment boundaries.
		cereal::PortableBinaryInputArchive iarchive(std::cin);
		
		{
			std::size_t input_count{};
			iarchive(cereal::make_size_tag(input_count));
			libbio_always_assert_eq(aligned_size, input_count);
		}
		
		// Reserve seq_count space for everything in order to avoid reallocations.
		std::vector <std::size_t> src_seq_indices(seq_count, 0);				// a_{k - 1}
		std::vector <std::size_t> dst_seq_indices(seq_count, 0);				// a_k
		std::vector <std::size_t> character_counts;								// Buffer for counting sort
		std::vector <std::size_t> sorted_divergence(seq_count, 0);				// s_k
		std::vector <std::size_t> divergence_value_indices(seq_count, 0);		// e_k
		std::vector <std::size_t> divergence_counts(seq_count, 0);				// t_k
		std::vector <std::size_t> new_eq_class_positions(seq_count, 0);
		std::size_t pos{}; // Reverse column index.
		
		// Initial order.
		std::iota(src_seq_indices.begin(), src_seq_indices.end(), 0);
		sorted_divergence.resize(1);
		divergence_counts.resize(1);
		divergence_counts[0] = input_count;
		// Process.
		while (reader.fill_buffer())
		{
			auto const *buffer(reader.buffer());
			for (std::size_t i(0); i < block_size; ++i)
			{
				auto const access_character([buffer, i, block_size](auto const seq_idx){
					auto const idx(i * block_size + seq_idx);
					auto const cc(buffer[idx]);
					return cc;
				});
				
				// Maintain the reverse column index.
				++pos;
				libbio_assert_lte(pos, aligned_size);
				
				// Update the sequence order.
				lb::counting_sort(src_seq_indices, dst_seq_indices, character_counts, access_character);
				auto const equivalence_class_count(character_counts.size());
#warning remember to resize character_counts in counting_sort.
				
				// Update the pBWT data structures.
				// We’re processing the columns in reverse order, so the data structures will reflect
				// longest common prefixes of the suffixes of the text, not vice-versa.
				// At this point we know that there are character_counts.size() equivalence classes
				// in the current column. Now we would like to determine, which divergence values
				// were replaced.
				std::size_t removed_divergence_values{};
				std::size_t first_removed_divergence_value_idx{SIZE_MAX};
				
				{
					// Divergenge value indices (e_k)
					// Position zero is handled separately.
					new_eq_class_positions.clear();
					for (std::size_t j(1); j < seq_count; ++j)
					{
						auto const seq_idx(dst_seq_indices[j]);
						if (access_character(j) != access_character(j - 1))
						{
							new_eq_class_positions.push_back(j);
							// If the new count is zero, increase the shift amount.
							auto const divergence_value_idx(divergence_value_indices[j]);
							auto &count(divergence_counts[divergence_value_idx]);
							--count;
							if (0 == count)
							{
								if (SIZE_MAX == first_removed_divergence_value_idx)
									first_removed_divergence_value_idx = divergence_value_idx;
								
								++removed_divergence_values;
								continue;
							}
						}
					
						divergence_value_indices[j] -= removed_divergence_values;
					}
				}
				
				{
					// Divergence values and counts (s_k and t_k)
					libbio_assert_eq(sorted_divergence.size(), divergence_counts.size());
					auto const count{sorted_divergence.size()};
					if (0 < removed_divergence_values)
					{
						// Shift the values and the counts.
						std::size_t shift_amt{};
						for (std::size_t j(first_removed_divergence_value_idx); j < count; ++j)
						{
							if (0 == divergence_counts[j])
							{
								++shift_amt;
								continue;
							}
							
							libbio_assert_lt(0, shift_amt);
							sorted_divergence[j - shift_amt] = sorted_divergence[j];
							divergence_counts[j - shift_amt] = divergence_counts[j];
						}
					}
					
					// Set the new divergence values.
					auto const new_count(count - removed_divergence_values + equivalence_class_count);
					libbio_assert_lte(new_count, 1 + prev_count);
					sorted_divergence.resize(new_count);
					divergence_counts.resize(new_count);
					sorted_divergence.back() = pos;
					divergence_counts.back() = equivalence_class_count;
					divergence_value_indices.front() = new_count - 1; // First row.
					for (auto const j : new_eq_class_positions)
						divergence_value_indices[j] = new_count - 1;
				}
				
				// Read the segment boundaries. The semi-repeat-free range will be [lb, rb].
				// Also maintain the memoized values in segment_costs.
				std::vector <dp_state> segment_costs(aligned_size);
				std::size_t const lb(aligned_size - pos - 1);
				std::size_t rb{};
				iarchive(rb);
				
				if (LENGTH_MAX == rb)
					continue;
				
				// [lb, rb] is a semi-repeat-free block. Iterate over the columns where the
				// number of equivalence classes changes.
				// FIXME: continue here.
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
	
	optimize_segmentation(args_info.sequence_list_arg);
	
	return EXIT_SUCCESS;
}
