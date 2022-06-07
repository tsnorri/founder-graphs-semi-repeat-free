/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_SORT_HH
#define FOUNDER_GRAPHS_SORT_HH

#include <iterator>						// std::random_access_iterator
#include <range/v3/utility/swap.hpp>	// ranges::swap


namespace founder_graphs {
	
	// Fwd.
	template <typename t_it>
	void sort(t_it begin, t_it end) requires(std::random_access_iterator <t_it>);
}


namespace founder_graphs::detail {

	template <typename t_it>
	inline void continue_sorting(t_it begin, t_it pivot, t_it end)
		requires(std::random_access_iterator <t_it>)
	{
		if ((pivot - begin) < (end - pivot))
		{
			::founder_graphs::sort(begin, pivot);
			::founder_graphs::sort(pivot, end);
		}
		else
		{
			::founder_graphs::sort(pivot, end);
			::founder_graphs::sort(begin, pivot);
		}
	}
}


namespace founder_graphs {
	
	// An (non-optimised) implementation of quicksort that only swaps elements.
	// Designed to work with ranges::views::zip and sdsl::int_vector.
	template <typename t_it>
	void sort(t_it begin, t_it end)
		requires(std::random_access_iterator <t_it>)
	{
		using ranges::swap;
			
		typedef typename std::iterator_traits <t_it>::value_type iterator_value_type;
	
		auto const count(end - begin);
		switch (count)
		{
			case 0:
			case 1:
				return;
			
			case 2:
			{
				if (*(end - 1) < *begin)
					swap(*(end - 1), *begin);
				return;
			}
		
			default:
			{
				auto const pivot_pos((end - begin - 1) / 2);
				iterator_value_type const pivot(*(begin + pivot_pos));
				
				auto lhs(begin);
				auto rhs(end - 1);
				
				while (*lhs < pivot) ++lhs;
				while (pivot < *rhs) --rhs;
				
				if (rhs <= lhs)
				{
					detail::continue_sorting(begin, rhs + 1, end);
					return;
				}
				
				swap(*lhs, *rhs);
				
				while (true)
				{
					do ++lhs; while (*lhs < pivot);
					do --rhs; while (pivot < *rhs);
					
					if (rhs <= lhs)
					{
						detail::continue_sorting(begin, rhs + 1, end);
						return;
					}
					
					swap(*lhs, *rhs);
				}
			}
		}
	}
}

#endif
