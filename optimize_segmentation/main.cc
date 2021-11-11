/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */


#include <cereal/archives/portable_binary.hpp>
#include <founder_graphs/basic_types.hh>
#include <libbio/assert.hh>
#include <libbio/file_handling.hh>
#include <range/v3/view/reverse.hpp>
#include <set>
#include <vector>
#include "cmdline.h"

namespace fg	= founder_graphs;
namespace lb	= libbio;
namespace rsv	= ranges::views;


namespace {
	
	typedef std::uint64_t score_type;
	
	
	// Interval without a score.
	struct interval
	{
		fg::length_type lb{};
		fg::length_type rb{};
		
		interval() = default;
		
		interval(fg::length_type const lb_, fg::length_type const rb_):
			lb(lb_),
			rb(rb_)
		{
		}
		
		bool is_valid() const { return fg::LENGTH_MAX != rb; }
		fg::length_type length() const { return rb - lb; }
	};
	
	struct interval_rb_cmp
	{
		bool operator()(interval const &lhs, interval const &rhs) const
		{
			return lhs.rb < rhs.rb;
		}
	};
	
	
	// Interval with a score and a pointer to the next interval.
	struct scored_interval
	{
		scored_interval	const	*next{};		// Due to how this struct is used, next needs to be const *.
		interval				location{};
		score_type				score{};
		
		scored_interval() = default;
		
		explicit scored_interval(interval const &location_):
			location(location_)
		{
		}
		
		scored_interval(fg::length_type const lb_, fg::length_type const rb_):
			location(lb_, rb_)
		{
		}
		
		bool is_valid() const { return location.is_valid(); }
		auto length() const { return location.length(); }
	};
	
	struct scored_interval_location_lb_cmp
	{
		bool operator()(scored_interval const &lhs, scored_interval const &rhs) const
		{
			return lhs.location.lb < rhs.location.rb;
		}
	};
	
	struct scored_interval_score_cmp
	{
		bool operator()(scored_interval const &lhs, scored_interval const &rhs) const
		{
			return lhs.score < rhs.score;
		}
	};
	
	typedef std::set <scored_interval, scored_interval_location_lb_cmp>	pending_interval_set;
	typedef std::multiset <scored_interval, scored_interval_score_cmp>	candidate_interval_set;
	
	
	// Read the input segmentation.
	fg::length_type read_input(std::istream &stream, std::vector <interval> &dst)
	{
		cereal::PortableBinaryInputArchive archive(stream);
		fg::length_type aligned_size{};
		archive(cereal::make_size_tag(aligned_size));
		dst.clear();
		dst.reserve(aligned_size);
		
		for (fg::length_type i(0); i < aligned_size; ++i)
		{
			auto const lb(aligned_size - i - 1);
			fg::length_type rb{};
			archive(fg::length_type(rb));
			dst.emplace_back(lb, rb + 1); // Store half-open intervals.
		}
		
		return aligned_size;
	}
	
	
	// Output the segmentation.
	void output_segmentation(scored_interval const &first_interval)
	{
		cereal::PortableBinaryOutputArchive archive(std::cout);
		
		// Count the blocks.
		fg::length_type block_count{};
		{
			scored_interval const *current_interval(&first_interval);
			while (current_interval->is_valid())
			{
				++block_count;
				current_interval = current_interval->next;
			}
		}
		
		// Output the count.
		archive(cereal::make_size_tag(block_count));
		
		// Output the intervals.
		{
			scored_interval const *current_interval(&first_interval);
			while (current_interval->is_valid())
			{
				archive(current_interval->location.lb);
				archive(current_interval->location.rb);
				current_interval = current_interval->next;
			}
		}
	}
	
	
	template <bool t_maximize>
	struct find_optimum
	{
		candidate_interval_set::iterator operator()(candidate_interval_set &is) const { return is.begin(); }
	};
	
	template <>
	struct find_optimum <true>
	{
		candidate_interval_set::iterator operator()(candidate_interval_set &is) const { return std::prev(is.end()); }
	};
	
	template <bool t_maximize, typename t_score_fn>
	void optimize(std::istream &stream, t_score_fn &&score_fn)
	{
		std::vector <interval> input_segmentation;
		auto const aligned_size(read_input(stream, input_segmentation));
		std::sort(input_segmentation.begin(), input_segmentation.end(), interval_rb_cmp());
		
		pending_interval_set pending_intervals;	// Left bounds are distinct.
		candidate_interval_set candidate_intervals;	// Scores are not distinct.
		
		// Add a sentinel so that we don’t need to check for emptiness.
		candidate_intervals.emplace(fg::LENGTH_MAX, fg::LENGTH_MAX);
		
		// For finding min. or max. value.
		find_optimum <t_maximize> find_optimum;
		
		for (auto const &interval : rsv::reverse(input_segmentation))
		{
			// Move from pending intervals s.t. interval.rb <= pending.lb.
			// node_handles ignore the comparator and the key cardinality of the container,
			// so they can be moved easily. extract() only handles single nodes, not ranges.
			while (!pending_intervals.empty())
			{
				auto const it(std::prev(pending_intervals.end()));
				if (! (interval.rb <= it->location.lb))
					break;
				
				auto nh(pending_intervals.extract(it));
				candidate_intervals.insert(std::move(nh));
			}
			
			// Add an entry for the current interval.
			scored_interval current_interval(interval);
			
			// Find the min./max. scoring interval.
			auto it(find_optimum(candidate_intervals));
			auto &next_interval(*it);
			current_interval.next = &next_interval;
			
			// Update the score.
			current_interval.score = score_fn(current_interval, next_interval);
			
			// Check if this was the last relevant interval.
			if (0 == interval.lb)
			{
				output_segmentation(current_interval);
				return;
			}
			
			// Move to pending.
			auto const res(pending_intervals.emplace(std::move(current_interval)));
			libbio_assert(res.second);
		}
		
		// For some reason the first block was not found.
		std::exit(EXIT_FAILURE);
	}
	
	
	void optimize_segmentation(std::istream &stream, gengetopt_args_info const &args_info)
	{
		if (args_info.max_number_of_blocks_given)
		{
			optimize <true>(stream, [](scored_interval const &current_interval, scored_interval const &next_interval){
				return 1 + next_interval.score;
			});
		}
		else if (args_info.min_block_length_given)
		{
			optimize <false>(stream, [](scored_interval const &current_interval, scored_interval const &next_interval){
				return std::max(current_interval.length(), next_interval.score);
			});
		}
		else
		{
			std::cerr << "Unknown mode given.\n";
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
	
	if (args_info.segmentation_arg)
	{
		lb::file_istream stream;
		lb::open_file_for_reading(args_info.segmentation_arg, stream);
		std::cin.tie(nullptr);
		optimize_segmentation(stream, args_info);
	}
	else
	{
		optimize_segmentation(std::cin, args_info);
	}
	
	return EXIT_SUCCESS;
}
