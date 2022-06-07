/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_LEXICOGRAPHIC_RANGE_HH
#define FOUNDER_GRAPHS_LEXICOGRAPHIC_RANGE_HH

#include <libbio/assert.hh>
#include <sdsl/suffix_array_algorithm.hpp>
#include <vector>


namespace founder_graphs::detail {
	
	template <typename t_wt, typename = void>
	struct has_r2d_res_type : std::false_type
	{
	};
	
	template <typename t_wt>
	struct has_r2d_res_type <t_wt, std::void_t <typename t_wt::r2d_res_type>> : std::true_type
	{
	};
	
	template <typename t_wt>
	constexpr inline bool has_r2d_res_type_v = std::is_base_of_v <std::true_type, has_r2d_res_type <t_wt>>;
	
	
	// Store the buffers for range_search_2d conditionally.
	template <typename t_wt, bool t_uses_r2d = has_r2d_res_type_v <t_wt>>
	struct lexicographic_range_pair_support
	{
		typedef typename t_wt::size_type	wt_size_type;
		
		constexpr static inline bool USES_RANGE_SEARCH_2D{true};
		
		std::vector <wt_size_type>			offsets;
		std::vector <wt_size_type>			ones_before_os;
		typename t_wt::point_vec_type		points;
	};
	
	template <typename t_wt>
	struct lexicographic_range_pair_support <t_wt, false>
	{
		constexpr static inline bool USES_RANGE_SEARCH_2D{false};
	};
}


namespace founder_graphs {
	
	template <typename t_csa>
	struct interval_symbols_context
	{
		typedef t_csa							csa_type;
		typedef typename csa_type::size_type	size_type;
		typedef typename csa_type::value_type	value_type;
		
		std::vector <value_type>		cs;
		std::vector <size_type>			rank_c_i;
		std::vector <size_type>			rank_c_j;
		
		interval_symbols_context() = default;
		
		interval_symbols_context(csa_type const &csa):
			cs(csa.sigma, 0),
			rank_c_i(csa.sigma, 0),
			rank_c_j(csa.sigma, 0)
		{
		}
	};
	
	
	template <typename t_csa>
	struct lexicographic_range
	{
		typedef t_csa								csa_type;
		typedef typename csa_type::size_type		size_type;
		typedef typename csa_type::char_type		char_type;
		typedef interval_symbols_context <csa_type>	interval_symbols_context_type;
		
		size_type lb{};
		size_type rb{};
		
		lexicographic_range() = default;
		
		explicit lexicographic_range(csa_type const &csa):
			rb(csa.size() - 1)
		{
		}
		
		lexicographic_range(size_type const lb_, size_type const rb_):
			lb(lb_),
			rb(rb_)
		{
		}
		
		size_type size() const { return rb - lb + 1; }
		bool empty() const { return rb < lb; }
		bool is_singleton() const { return lb == rb; }
		bool has_prefix(lexicographic_range const other) const { return other.lb <= lb && rb <= other.rb; }
		
		void reset(csa_type const &csa)
		{
			lb = 0;
			rb = csa.size() - 1;
		}
		
		size_type backward_search(csa_type const &csa, char_type const cc)
		{
			return sdsl::backward_search(csa, lb, rb, cc, lb, rb);
		}
		
		template <typename t_it>
		size_type backward_search(csa_type const &csa, t_it begin, t_it it)
		{
			while (it != begin && 0 < rb + 1 - lb)
			{
				--it;
				backward_search(csa, *it);
			}
			
			return rb + 1 - lb;
		}
		
		template <typename t_it>
		size_type backward_search_h(csa_type const &csa, t_it begin, t_it it)
		{
			if (!backward_search(csa, '#'))
				return 0;
			return backward_search(csa, begin, it);
		}
		
		size_type forward_search(csa_type const &reverse_csa, char_type const cc)
		{
			// Assume that the index is reversed.
			return sdsl::backward_search(reverse_csa, lb, rb, cc, lb, rb);
		}
		
		template <typename t_it>
		size_type forward_search(csa_type const &reverse_csa, t_it it, t_it const end)
		{
			// Assume that the index is reversed.
			for (; it != end && 0 < rb + 1 - lb; ++it)
				backward_search(reverse_csa, *it);
			
			return rb + 1 - lb;
		}
		
		template <typename t_it>
		size_type forward_search_h(csa_type const &reverse_csa, t_it it, t_it end)
		{
			// Assume that the index is reversed.
			// ‘#’ should be the last character.
			if (!forward_search(reverse_csa, it, end))
				return 0;
			return forward_search(reverse_csa, '#');
		}
		
		size_type interval_symbols(csa_type const &csa, interval_symbols_context_type &ctx) const
		{
			size_type retval{};
			// interval_symbols takes a half-open range.
			csa.wavelet_tree.interval_symbols(lb, 1 + rb, retval, ctx.cs, ctx.rank_c_i, ctx.rank_c_j);
			return retval;
		}
	};
	
	
	template <
		typename t_csa,
		typename t_reverse_csa = t_csa,
		typename t_base = detail::lexicographic_range_pair_support <typename t_csa::wavelet_tree_type>
	>
	struct lexicographic_range_pair : public t_base
	{
		// We could also use std::common_type to find a suitable size_type.
		static_assert(std::is_same_v <typename t_csa::size_type, typename t_reverse_csa::size_type>);
		
		using t_base::USES_RANGE_SEARCH_2D;
		
		typedef t_csa									csa_type;
		typedef t_reverse_csa							reverse_csa_type;
		typedef typename csa_type::size_type			size_type;
		typedef typename csa_type::wavelet_tree_type	wavelet_tree_type;
		typedef typename csa_type::char_type			char_type;
		typedef lexicographic_range <csa_type>			lexicographic_range_type;
		
		lexicographic_range_type	range{};
		lexicographic_range_type	co_range{};
		
		lexicographic_range_pair() = default;
		
		explicit lexicographic_range_pair(csa_type const &csa):
			range(csa),
			co_range(range)
		{
		}
		
		lexicographic_range_pair(csa_type const &csa, reverse_csa_type const &):
			range(csa),
			co_range(range)
		{
		}
		
		lexicographic_range_pair(size_type const lb, size_type const rb, size_type const rlb, size_type const rrb):
			range(lb, rb),
			co_range(rlb, rrb)
		{
		}
		
		size_type size() const { return range.size(); }
		bool empty() const { return range.empty(); }
		bool is_singleton() const { return range.is_singleton(); }
		bool has_prefix(lexicographic_range <t_csa> const other) const { return range.has_prefix(other); }
		bool has_prefix(lexicographic_range_pair <t_csa> const other) const { return range.has_prefix(other.range); }
		
		
		void reset(csa_type const &csa)
		{
			range.reset(csa);
			co_range.reset(csa);
		}
		
		
		// Maintain both ranges.
		// Having this if USES_RANGE_SEARCH_2D is false seems too error-prone.
		size_type backward_search(csa_type const &csa, char_type const cc)
			requires(USES_RANGE_SEARCH_2D)
		{
			libbio_assert_neq(cc, 0);
			auto const kk(csa.wavelet_tree.range_search_2d(range.lb, range.rb, 0, cc - 1, this->offsets, this->ones_before_os, this->points, false));
			auto const retval(range.backward_search(csa, cc));
			co_range.lb += kk;
			co_range.rb = co_range.lb + retval - 1;
			return retval;
		}
		
		
		template <typename t_it>
		size_type backward_search(csa_type const &csa, t_it begin, t_it it)
			requires(USES_RANGE_SEARCH_2D)
		{
			while (it != begin && 0 < range.rb + 1 - range.lb)
			{
				--it;
				backward_search(csa, *it);
			}
			
			return range.rb + 1 - range.lb;
		}
		
		
		template <typename t_it>
		size_type backward_search_h(csa_type const &csa, t_it begin, t_it end)
			requires(USES_RANGE_SEARCH_2D)
		{
			if (!backward_search(csa, '#'))
				return 0;
			return backward_search(csa, begin, end);
		}
		
		
		template <typename t_it>
		size_type backward_search(csa_type const &csa, reverse_csa_type const &reverse_csa, t_it begin, t_it end)
			requires(!USES_RANGE_SEARCH_2D)
		{
			auto const retval(range.backward_search(csa, begin, end));
			co_range.forward_search(reverse_csa, begin, end);
			libbio_assert_eq(retval, co_range.size());
			return retval;
		}
		
		
		template <typename t_it>
		size_type backward_search_h(csa_type const &csa, reverse_csa_type const &reverse_csa, t_it begin, t_it end)
			requires(!USES_RANGE_SEARCH_2D)
		{
			auto const retval(range.backward_search_h(csa, begin, end));
			co_range.forward_search_h(reverse_csa, begin, end);
			libbio_assert_eq(retval, co_range.size());
			return retval;
		}
	};
	
}

#endif
