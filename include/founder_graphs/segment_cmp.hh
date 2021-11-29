/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_SEGMENT_CMP_HH
#define FOUNDER_GRAPHS_SEGMENT_CMP_HH

#include <compare>
#include <libbio/cxxcompat.hh>
#include <string>


namespace founder_graphs {
	
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
			std::size_t li(0);
			std::size_t ri(0);
			std::size_t rhs_gap_count(0);
			std::size_t const lc(lhs.size());
			std::size_t const rc(rhs.size());
			while (li < lc && ri < rc)
			{
				// Check for a gap.
				while ('-' == rhs[ri])
				{
					++ri;
					++rhs_gap_count;
					if (ri == rc)
						goto exit_loop;
				}
				
				// Compare and check for equality.
				auto const res(lhs[li] <=> rhs[ri]);
				if (0 != res) // FIXME: for some reason, some versions of libc++ do not have std::is_neq(). (std::is_neq(res))
					return res;
				
				// Continue.
				++li;
				++ri;
			}
			
			// Handle trailing gaps.
			while (ri < rc)
			{
				if ('-' != rhs[ri])
					break;
				
				++ri;
				++rhs_gap_count;
			}
			
		exit_loop:
			// The strings have equal (possibly empty) prefixes. Compare the lengths.
			return (lc <=> (rc - rhs_gap_count));
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
			return std::is_gt(strong_order(rhs, lhs));
		}
		
		// For pairs of map keys.
		bool operator()(std::string const &lhs, std::string const &rhs) const
		{
			return lhs < rhs;
		}
	};
}

#endif
