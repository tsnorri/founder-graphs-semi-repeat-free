/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_COUNT_DISTINCT_HH
#define FOUNDER_GRAPHS_COUNT_DISTINCT_HH

#include <iterator>


namespace founder_graphs {
	
	// FIXME: enable_if t_iterator is not single-pass.
	template <typename t_iterator>
	std::size_t count_distinct(t_iterator it, t_iterator const end)
	{
		if (it == end)
			return 0;

		std::size_t retval{1};
		auto prev(it);
		it = std::next(it);
		while (it != end)
		{
			if (*it != *prev)
				++retval;

			prev = it;
			it = std::next(it);
		}

		return retval;
	}
}

#endif
