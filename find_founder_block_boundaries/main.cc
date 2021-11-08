/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */


#include <cereal/archives/portable_binary.hpp>
#include <founder_graphs/basic_types.hh>
#include <founder_graphs/cst.hh>
#include <founder_graphs/msa_index.hh>
#include <founder_graphs/reverse_msa_reader.hh>
#include <iostream>
#include <libbio/assert.hh>
#include <libbio/file_handling.hh>
#include <libbio/utility.hh>
#include <range/v3/view/subrange.hpp>
#include <set>
#include <vector>
#include "cmdline.h"

namespace fg	= founder_graphs;
namespace lb	= libbio;


namespace {
	
	struct lexicographic_range
	{
		typedef fg::csa_type::size_type	csa_size_type;
		
		csa_size_type	lb{};
		csa_size_type	rb{};
		
		lexicographic_range() = default;
		
		lexicographic_range(csa_size_type const lb_, csa_size_type const rb_):
			lb(lb_),
			rb(rb_)
		{
		}
		
		std::size_t interval_length() const { return rb - lb + 1; }
	};
	
	
	struct node_span
	{
		typedef fg::cst_type::node_type	node_type;
		
		struct sentinel_tag {};
		
		node_type	node{};			// CST node.
		std::size_t	length_sum{};	// Cumulative sum.
		std::size_t	sequence{};		// Sequence identifier.
		
		node_span() = default;
		
		node_span(fg::cst_type::node_type const &node_, std::size_t const sequence_):
			node(node_),
			length_sum(0),
			sequence(sequence_)
		{
		}
		
		// The constructor below results in the correct interval length as long as
		// that of node does not check for a non-empty interval.
		static_assert(std::is_unsigned_v <fg::cst_interval_endpoint_type>);
		explicit node_span(sentinel_tag const &):
			node(fg::CST_INTERVAL_ENDPOINT_MAX, fg::CST_INTERVAL_ENDPOINT_MAX - 1),
			sequence(SIZE_MAX)
		{
		}
		
		std::size_t interval_length() const { return node.j - node.i + 1; }
		bool is_sentinel() const { return fg::CST_INTERVAL_ENDPOINT_MAX == node.i; }
		bool encloses(node_span const &other) const { return node.i <= other.node.i && other.node.j <= node.j; }
	};


	std::ostream &operator<<(std::ostream &os, node_span const &span)
	{
		os << "Node: [" << span.node.i << ", " << span.node.j << "] length_sum: " << span.length_sum << " seq: " << span.sequence;
		return os;
	}
	
	
	struct node_span_cmp
	{
		bool operator()(node_span const &lhs, lexicographic_range const &rhs) const { return lhs.node.j < rhs.lb; }
		bool operator()(lexicographic_range const &lhs, node_span const &rhs) const { return lhs.rb < rhs.node.i; }
	};
	
	
	typedef std::vector <node_span> node_span_vector;
	typedef std::pair <node_span_vector::const_iterator, node_span_vector::const_iterator> node_span_vector_range;


	// For debugging.
	bool check_node_spans(node_span_vector const &vec)
	{
		bool retval{true};
		std::set <std::size_t> seen_seq_numbers;
		for (auto const &span : vec)
		{
			auto const res(seen_seq_numbers.insert(span.sequence));
			if (!res.second)
			{
				std::cerr << "Sequence number " << span.sequence << " was already assigned.\n";
				retval = false;
			}
		}
		return retval;
	}
	
	
	template <typename t_ds>
	void read_from_file(char const *path, t_ds &ds)
	{
		lb::file_istream stream;
		lb::open_file_for_reading(path, stream);
		cereal::PortableBinaryInputArchive iarchive(stream);
		iarchive(ds);
	}
	
	
	void find_founder_block_boundaries(char const *sequence_list_path, char const *cst_path, char const *msa_index_path, fg::reverse_msa_reader &reader)
	{
		lb::log_time(std::cerr) << "Loading the data structures…\n";

		// Open the inputs.
		lb::file_istream sequence_path_stream;
		lb::open_file_for_reading(sequence_list_path, sequence_path_stream);
		
		fg::cst_type cst;
		fg::msa_index msa_index;
		
		read_from_file(cst_path, cst);
		read_from_file(msa_index_path, msa_index);
		
		{
			std::string line;
			while (std::getline(sequence_path_stream, line))
				reader.add_file(line);
		}
		
		// Prepare for output.
		cereal::PortableBinaryOutputArchive archive(std::cout);
		
		// Process.
		{
			// Prepare the reader.
			reader.prepare();
			auto const seq_count(reader.handle_count());
			auto const aligned_size(reader.aligned_size());
			
			// Output the aligned size.
			archive(cereal::make_size_tag(aligned_size));
			
			// For storing the intervals, a slightly different idea (w.r.t. the one in the paper) is used.
			// Store the lexicographic ranges in a vector, sort and calculate the cumulative sum of the lengths of lexicographic ranges. (The sum of the lengths is important b.c. there can be nodes between the nodes in the same subtree, and we want to exclude them.) Process from left to right as follows.
			// – Set L and R to the vector index the current node.
			// – Use the parent operation on the node and then the equal_range operation. Check if the lexicographic range of the found parent matches the boundaries of the equal range.
			// 		– If it does, store the vector index range [L, R].
			// 		– If it does not, the string depth of the parent node plus one should be stored with the nodes in the previously found equivalence class (i.e. nodes between L and R).
			// – Continue from the next node w.r.t. R.
			// 
			// The lexicographic ranges in the vector need not be replaced at any point b.c. if some nodes have a valid parent node (in the sense that it does not have any other child nodes), the same nodes can be used to determine the range of the parent node.
			std::vector <lexicographic_range> lexicographic_ranges(seq_count, lexicographic_range(0, cst.csa.size() - 1));
			node_span_vector node_spans(1 + seq_count);
			std::vector <std::size_t> string_depths(seq_count);
			std::size_t pos{0};
			std::vector <std::size_t> handled_sequences; // For debugging.
			lb::log_time(std::cerr) << "Finding founder block boundaries…\n";
			while (reader.fill_buffer(
				[
					&reader,
					&pos,
					seq_count,
					aligned_size,
					&lexicographic_ranges,
					&cst,
					&node_spans,
					&archive,
					&string_depths,
					&msa_index,
					&handled_sequences
				](bool const did_fill){
					
					if (!did_fill)
						return false;
					
					auto const &buffer(reader.buffer());
					auto const block_size(reader.block_size());
					// Read the characters.
					for (std::size_t i(0); i < block_size; ++i)
					{
						++pos;
						libbio_assert_lte(pos, aligned_size);

						if (0 == pos % 10000)
							lb::log_time(std::cerr) << "Position " << pos << '/' << aligned_size << "…\n";
						
						for (std::size_t j(0); j < seq_count; ++j)
						{
							auto const idx((j + 1) * block_size - i - 1);
							auto const cc(buffer[idx]);
							auto &lex_range(lexicographic_ranges[j]);
							
							// Skip gap characters.
							if ('-' != cc)
							{
								sdsl::backward_search(cst.csa, lex_range.lb, lex_range.rb, cc, lex_range.lb, lex_range.rb);
								libbio_always_assert_lte(lex_range.lb, lex_range.rb);
							}
							
							// Convert to a CST node and store the sequence identifier.
							auto &span(node_spans[j]);
							span = node_span(cst.node(lex_range.lb, lex_range.rb), j);
						}
						
						// Sentinel.
						node_spans[seq_count] = node_span(node_span::sentinel_tag{});

						// Sort by the left bound and the in reverse by the right bound.
						// Since the nodes represent lexicographic ranges, they can overlap only by being nested.
						std::sort(node_spans.begin(), node_spans.end(), [](auto const &lhs, auto const &rhs){
							return std::make_tuple(lhs.node.i, lhs.node.j) < std::make_tuple(rhs.node.i, rhs.node.j);
						});

						// Update the cumulative sum.
						// Ignore nested intervals.
						{
							auto &first_span(node_spans.front());
							first_span.length_sum = 0;
							auto const count(node_spans.size()); // Consider the sentinel, too.
							if (1 < count)
							{
								// If count == 2, second_span is the sentinel.
								auto &second_span(node_spans[1]);
								second_span.length_sum = first_span.length_sum + first_span.interval_length();
								for (std::size_t j(2); j < count; ++j)
								{
									// Don’t consider the interval of the current span, just those of the two previous ones.
									auto &span(node_spans[j]);
									auto const &prev1(node_spans[j - 1]);
									auto const &prev2(node_spans[j - 2]);
									if (prev1.encloses(prev2)) // Safe b.c. of the comparison operator used when sorting.
										span.length_sum = prev1.length_sum + prev1.interval_length() - prev2.interval_length();
									else
										span.length_sum = prev1.length_sum + prev1.interval_length();
								}
							}
						}

						// Make sure the altorighm for updating the cumulative sum is correct.
						try
						{
							libbio_assert(std::is_sorted(node_spans.begin(), node_spans.end(), [](auto const &lhs, auto const &rhs){
								return lhs.length_sum < rhs.length_sum;
							}));
						}
						catch (lb::assertion_failure_exception const &exc)
						{
							std::cerr << "Node spans:\n";
							for (auto const &span : node_spans)
								std::cerr << span << '\n';
							throw exc;
						}
						
						// Check if the current block is semi-repeat-free.
						libbio_assert(node_spans.back().is_sentinel());
						if (node_spans.back().length_sum != seq_count)
						{
							archive(fg::length_type(fg::LENGTH_MAX));
							continue;
						}
						
						// At this point the current block or column range [(aligned_size - pos), aligned_size)
						// is semi-repeat-free. Try to move the right bound as far left as possible.
						{
							node_span_cmp cmp;
							
							// Fill with placeholder values for extra safety.
							std::fill(string_depths.begin(), string_depths.end(), SIZE_MAX);
							
							auto node_it(node_spans.cbegin());
							auto const node_end(node_spans.cend() - 1); // Don’t handle the sentinel.
							handled_sequences.clear();
							while (node_it != node_end)
							{
								auto &span(*node_it);
								node_span_vector_range span_equivalence_class(node_spans.cend(), node_spans.cend());
								auto node(span.node); // Copy.
								// On the first round, determine the initial equivalence class.
								// Then proceed to the ancestor nodes.
								while (true)
								{
									lexicographic_range const rng(cst.lb(node), cst.rb(node));
									// Check if the length of the lexicographic range increased.
									auto equal_range(std::equal_range(node_it, node_spans.cend(), rng, cmp));
									libbio_assert_neq(equal_range.second, node_spans.end()); // second should always point to a valid element b.c. the last element is the sentinel.
									libbio_assert_lt(equal_range.first, equal_range.second); // The range should always be non-empty b.c. the node itself should be inside it.
									// Stop if we extended too much.
									if (equal_range.second->length_sum - equal_range.first->length_sum != rng.interval_length())
										break;
									// Otherwise store the new range.
									span_equivalence_class = equal_range;
									// Continue from the parent node.
									node = cst.parent(node);
								}
								
								// cst.parent() should be called at least once b.c. the initial range is semi-repeat-free.
								libbio_assert_neq(span.node, node);
								
								// Determine the string depth.
								auto const string_depth(1 + cst.depth(node));
								libbio_assert_lt(0, string_depth);
								for (auto const &span : ranges::subrange(span_equivalence_class.first, span_equivalence_class.second))
								{
									string_depths[span.sequence] = string_depth;
#ifndef NDEBUG
									handled_sequences.push_back(span.sequence);
#endif
								}
								
								node_it = span_equivalence_class.second;
							}
						}

						// Find the minimum right bound for the block by counting characters.
						std::size_t const block_lb{aligned_size - pos};
						std::size_t max_block_rb{0};
						for (std::size_t j(0); j < seq_count; ++j)
						{
							try
							{
								auto const string_depth(string_depths[j]);
								libbio_assert_neq(string_depth, SIZE_MAX);
								auto const &seq_idx(msa_index.sequence_indices[j]);
								auto const non_gap_count_before(seq_idx.rank0_support(block_lb));
								auto const non_gap_rb(non_gap_count_before + string_depth);
								auto const block_rb(seq_idx.select0_support(1 + non_gap_rb));
								libbio_always_assert_lt(block_rb, SIZE_MAX);
								max_block_rb = std::max(max_block_rb, std::size_t(block_rb));
							}
							catch (lb::assertion_failure_exception const &exc)
							{
								std::cerr << "String depth for sequence " << j << '/' << seq_count << " was not set.\n";

								std::cerr << "Handled sequences:\n";
								std::sort(handled_sequences.begin(), handled_sequences.end());
								for (auto const idx : handled_sequences)
									std::cerr << idx << '\n';

								std::cerr << "Node spans (" << node_spans.size() << "):\n";
								for (auto const &span : node_spans)
									std::cerr << span << '\n';

								auto sorted_node_spans(node_spans);
								std::sort(sorted_node_spans.begin(), sorted_node_spans.end(), [](auto const &lhs, auto const &rhs){
									return lhs.sequence < rhs.sequence;
								});
								std::cerr << "Sorted node spans (" << sorted_node_spans.size() << "):\n";
								for (auto const &span : sorted_node_spans)
									std::cerr << span << '\n';
								{
									std::cerr << "Equivalent sequence numbers:\n";
									std::size_t prev_eq(SIZE_MAX);
									for (std::size_t k(1); k < sorted_node_spans.size(); ++k)
									{
										if (sorted_node_spans[k - 1].sequence == sorted_node_spans[k].sequence)
										{
											if (k - 1 != prev_eq)
												std::cerr << sorted_node_spans[k - 1] << '\n';
											std::cerr << sorted_node_spans[k] << '\n';
											prev_eq = k;
										}
									}
								}

								throw exc;
							}
						}
						
						// Output max_block_rb.
						// The semi-repeat-free range will be [block_lb, block_rb].
						archive(fg::length_type(max_block_rb));
					}
					
					return true;
				}
			));
		}
		
		std::cout << std::flush;
		lb::log_time(std::cerr) << "Done.\n";
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
	
	if (args_info.bgzip_input_flag)
	{
		fg::bgzip_reverse_msa_reader reader;
		find_founder_block_boundaries(args_info.sequence_list_arg, args_info.cst_arg, args_info.msa_index_arg, reader);
	}
	else
	{
		fg::text_reverse_msa_reader reader;
		find_founder_block_boundaries(args_info.sequence_list_arg, args_info.cst_arg, args_info.msa_index_arg, reader);
	}
	
	return EXIT_SUCCESS;
}
