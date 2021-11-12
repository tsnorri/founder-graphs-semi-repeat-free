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
	
	typedef fg::length_type score_type;
	
	constexpr static inline score_type	SCORE_MAX{std::numeric_limits <score_type>::max()};
	
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
	
	struct interval_lb_cmp
	{
		bool operator()(interval const &lhs, interval const &rhs) const
		{
			return std::make_tuple(lhs.lb, lhs.rb) < std::make_tuple(rhs.lb, rhs.rb);
		}
	};
	
	struct interval_rb_cmp
	{
		bool operator()(interval const &lhs, interval const &rhs) const
		{
			return std::make_tuple(lhs.rb, lhs.lb) < std::make_tuple(rhs.rb, rhs.lb);
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
		
		scored_interval(fg::length_type const lb_, fg::length_type const rb_, score_type const score_):
			location(lb_, rb_),
			score(score_)
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
	
	struct scored_interval_location_rb_cmp
	{
		bool operator()(scored_interval const &lhs, scored_interval const &rhs) const
		{
			return lhs.location.rb < rhs.location.rb;
		}
	};
	
	struct scored_interval_score_cmp
	{
		bool operator()(scored_interval const &lhs, scored_interval const &rhs) const
		{
			return lhs.score < rhs.score;
		}
	};
	
	typedef std::set <scored_interval, scored_interval_location_lb_cmp>	interval_by_lb_set;
	typedef std::multiset <scored_interval, scored_interval_score_cmp>	interval_by_score_set;
	
	
	struct candidate_interval_position
	{
		typedef interval_by_score_set::iterator	pointer_type;
		typedef std::int64_t					position_type;
		
		position_type	position{}; // Cache b.c. calculation can be expensive.
		pointer_type	pointer{};
		
		static position_type calculate_position(fg::length_type const j, score_type const s)
		{
			// We first rotate the point (j, s) by 45° and then project to the X axis
			// while also applying some scaling. The resulting very simple tranformation is j - s.
			libbio_assert_lte(0, j);
			libbio_assert_lte(0, s);
			position_type const j_(j);
			position_type const s_(s);
			libbio_assert_eq(fg::length_type(j_), j);
			libbio_assert_eq(fg::length_type(s_), s);
			position_type const retval(position_type(j) - position_type(s));
			libbio_assert_lte(retval, j_);
			return retval;
		}
		
		static position_type calculate_position_lb(scored_interval const &interval_) { return calculate_position(interval_.location.lb, interval_.score); }
		static position_type calculate_position_rb(scored_interval const &interval_) { return calculate_position(interval_.location.rb, interval_.score); }
		static position_type calculate_position_lb(pointer_type const pointer_) { return calculate_position_lb(*pointer_); }
		static position_type calculate_position_rb(pointer_type const pointer_) { return calculate_position_rb(*pointer_); }
		
		candidate_interval_position(position_type const position_, pointer_type const pointer_):
			position(position_),
			pointer(pointer_)
		{
		}
		
		// Use left bound by default.
		explicit candidate_interval_position(pointer_type const pointer_):
			candidate_interval_position(calculate_position_lb(pointer_), pointer_)
		{
		}
		
		bool operator<(candidate_interval_position const &other) const { return position < other.position; }
	};
	
	struct candidate_interval_position_cmp
	{
		typedef std::true_type is_transparent;
		
		bool operator()(candidate_interval_position const &lhs, candidate_interval_position const &rhs) const { return lhs.position < rhs.position; }
		bool operator()(candidate_interval_position::position_type const lhs, candidate_interval_position const &rhs) const { return lhs < rhs.position; }
		bool operator()(candidate_interval_position const &lhs, candidate_interval_position::position_type const rhs) const { return lhs.position < rhs; }
	};
	
	typedef std::multiset <candidate_interval_position, candidate_interval_position_cmp> candidate_interval_position_set;
	
	
	// Read the input segmentation.
	fg::length_type read_input(std::istream &stream, std::vector <interval> &dst)
	{
		cereal::PortableBinaryInputArchive archive(stream);
		fg::length_type aligned_size{};
		archive(cereal::make_size_tag(aligned_size));
		dst.clear();
		dst.reserve(1 + aligned_size);
		
		for (fg::length_type i(0); i < aligned_size; ++i)
		{
			auto const lb(aligned_size - i - 1);
			fg::length_type rb{};
			archive(rb);
			if (fg::LENGTH_MAX == rb)
				continue;
			libbio_assert_lte(lb, rb);
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
			libbio_assert_eq(0, current_interval->location.lb);
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
			fg::length_type prev_rb{};
			while (current_interval->is_valid())
			{
				auto const rb(current_interval->location.rb);
				libbio_assert_lt(prev_rb, rb);
				archive(rb);
				current_interval = current_interval->next;
				prev_rb = rb;
			}
		}
	}
	
	
	void max_number_of_blocks(std::istream &stream)
	{
		// Use an O(n log n) time algorithm to optimize.
		lb::log_time(std::cerr) << "Loading the input segmentation…\n";
		std::vector <interval> input_segmentation;
		auto const aligned_size(read_input(stream, input_segmentation));
		lb::log_time(std::cerr) << "Sorting…\n";
		std::sort(input_segmentation.begin(), input_segmentation.end(), interval_rb_cmp());
		
		interval_by_lb_set pending_intervals;	// Left bounds are distinct.
		interval_by_score_set candidate_intervals;	// Scores are not distinct.
		// We actually don’t need the right bounds in candidate_intervals.
		
		// Add a sentinel.
		candidate_intervals.emplace(aligned_size, fg::LENGTH_MAX);
		
		lb::log_time(std::cerr) << "Optimizing…\n";
		std::size_t count{};
		for (auto const &interval : rsv::reverse(input_segmentation))
		{
			++count;
			if (0 == count % 10000000)
				lb::log_time(std::cerr) << "Interval " << count << '/' << input_segmentation.size() << " (at most)…\n";

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
		
			// Find the max. scoring interval.
			{
				auto it(std::prev(candidate_intervals.end()));
				auto &next_interval(*it);
				libbio_assert_lte(current_interval.location.rb, next_interval.location.lb);
				current_interval.next = &next_interval;
		
				// Update the score and the boundaries.
				current_interval.score = 1 + next_interval.score;
				current_interval.location.rb = next_interval.location.lb;
			}
		
			// Check if this was the last relevant interval.
			if (0 == interval.lb)
			{
				output_segmentation(current_interval);
				return;
			}
		
			// Move to pending.
			{
				auto const res(pending_intervals.emplace(std::move(current_interval)));
				libbio_assert(res.second);
			}
		}
		
		// For some reason the first block was not found.
		std::exit(EXIT_FAILURE);
	}
	
	
	void min_block_length(std::istream &stream)
	{
		auto const score_fn([](scored_interval const &current, scored_interval const &next){
			return std::max(next.location.lb - current.location.lb, next.score);
		});
		
		auto const assign_fn([](scored_interval &current, scored_interval const &next, score_type const score){
			current.score = score;
			current.next = &next;
			current.location.rb = next.location.lb;
		});
		
		// Use an O(n log n) time algorithm to optimize.
		typedef std::multiset <scored_interval, scored_interval_location_lb_cmp> candidate_interval_by_lb_set;
		
		// Use an O(n log n) time algorithm to optimize.
		lb::log_time(std::cerr) << "Loading the input segmentation…\n";
		std::vector <interval> input_segmentation;
		auto const aligned_size(read_input(stream, input_segmentation));
		lb::log_time(std::cerr) << "Sorting…\n";
		std::sort(input_segmentation.begin(), input_segmentation.end(), interval_rb_cmp());
		
		interval_by_lb_set pending_intervals;	// Left bounds are distinct.
		interval_by_lb_set candidates_by_lb;
		interval_by_score_set candidates_by_score;
		candidate_interval_position_set candidate_interval_positions;
		
		// Add sentinels.
		candidates_by_lb.emplace(aligned_size, fg::LENGTH_MAX, 0);
		candidates_by_score.emplace(aligned_size, fg::LENGTH_MAX, SCORE_MAX); // Make sure this one does not get chosen ever.
		
		lb::log_time(std::cerr) << "Optimizing…\n";
		std::size_t count{};
		for (auto const &interval : rsv::reverse(input_segmentation))
		{
			++count;
			if (0 == count % 10000000)
				lb::log_time(std::cerr) << "Interval " << count << '/' << input_segmentation.size() << " (at most)…\n";
			
			// Move from pending intervals s.t. interval.rb <= pending.lb.
			// node_handles ignore the comparator and the key cardinality of the container,
			// so they can be moved easily. extract() only handles single nodes, not ranges.
			while (!pending_intervals.empty())
			{
				auto const pending_it(std::prev(pending_intervals.end()));
				if (! (interval.rb <= pending_it->location.lb))
					break;
			
				auto nh(pending_intervals.extract(pending_it));
				auto const candidate_it(candidates_by_score.insert(std::move(nh)));
				candidate_interval_positions.emplace(candidate_it); // Uses left bound.
			}
			
			// Add an entry for the current interval.
			scored_interval current_interval(interval);
			
			// Since we are iterating in reverse order by right bounds, we can partition
			// candidate_interval_positions by pos and move the right side to candidates_by_lb.
			{
				auto const pos(candidate_interval_position::calculate_position_lb(current_interval));
				auto it(candidate_interval_positions.lower_bound(pos));
				auto const it_(it);
				auto const end(candidate_interval_positions.end());
				while (it != end)
				{
					auto nh(candidates_by_score.extract(it->pointer));
					auto const res(candidates_by_lb.insert(std::move(nh)));
					libbio_assert(res.inserted);
					++it;
				}
				candidate_interval_positions.erase(it_, end);
			}
			
			// Find the min. scoring interval.
			{
				auto const &next1(*candidates_by_score.begin());
				auto const &next2(*candidates_by_lb.begin());
				auto const score1(score_fn(current_interval, next1));
				auto const score2(score_fn(current_interval, next2));
				if (score1 < score2)
					assign_fn(current_interval, next1, score1);
				else
					assign_fn(current_interval, next2, score2);
			}
			
			// Check if this was the last relevant interval.
			if (0 == interval.lb)
			{
				output_segmentation(current_interval);
				return;
			}
			
			// Move to pending.
			{
				auto const res(pending_intervals.emplace(std::move(current_interval)));
				libbio_assert(res.second);
			}
		}
	}
	
	
	void optimize_segmentation(std::istream &stream, gengetopt_args_info const &args_info)
	{
		if (args_info.max_number_of_blocks_given)
			max_number_of_blocks(stream);
		else if (args_info.min_block_length_given)
			min_block_length(stream);
		else
		{
			std::cerr << "Unknown mode given.\n";
			std::exit(EXIT_FAILURE);
		}
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
