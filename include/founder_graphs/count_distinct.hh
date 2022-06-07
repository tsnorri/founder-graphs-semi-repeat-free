/*
 * Copyright (c) 2021-2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_COUNT_DISTINCT_HH
#define FOUNDER_GRAPHS_COUNT_DISTINCT_HH

#include <iterator>


namespace founder_graphs {
	
	// Collapse groups of equal items.
	// Works only with default constructible types and non-single-pass iterators.
	// FIXME: require/enable_if iterator_type is not single-pass.
	template <typename t_range, bool t_should_remove_cvref>
	class count_distinct
	{
	public:
		typedef decltype(std::begin(std::declval <t_range>()))	iterator_type;
		typedef decltype(*std::declval <iterator_type>())		iterator_value_type;
		typedef std::conditional_t <
			t_should_remove_cvref,
			std::remove_cvref_t <iterator_value_type>,
			iterator_value_type
		>														value_type_;
		
		struct call_type
		{
			typedef value_type_ value_type;
			
			value_type	value{};
			std::size_t	size{};
			bool		is_valid{false};
			
		public:
			call_type() = default;
			
			call_type(iterator_value_type &&value_):
				value(std::forward <iterator_value_type>(value_)),
				size(1),
				is_valid(true)
			{
			}
		};
		
	protected:
		iterator_type	m_it;
		iterator_type	m_end;
		
	public:
		count_distinct() = default;
		
		count_distinct(t_range &range): // Make sure the range is owned by someone else.
			m_it(std::begin(range)),
			m_end(std::end(range))
		{
		}
		
		call_type operator()()
		{
			if (m_it == m_end)
				return {};
			
			call_type retval(*m_it);
			++m_it;
			while (m_it != m_end && *m_it == retval.value)
			{
				++retval.size;
				++m_it;
			}
			
			return retval;
		}
	};
	
	
	template <bool t_should_remove_cvref, typename t_range>
	auto make_count_distinct(t_range &range) -> count_distinct <t_range, t_should_remove_cvref>
	{
		return {range};
	}
}

#endif
