/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_UTILITY_HH
#define FOUNDER_GRAPHS_UTILITY_HH

#include <cereal/cereal.hpp>
#include <cstddef>
#include <libbio/assert.hh>
#include <libbio/file_handle.hh>
#include <sdsl/int_vector.hpp>
#include <sdsl/structure_tree.hpp>
#include <sdsl/util.hpp>
#include <tuple>


namespace founder_graphs::detail {
	
	template <std::size_t t_i, std::size_t t_limit>
	struct inline_for_helper
	{
		template <typename t_fn>
		constexpr static inline void call(t_fn &&fn) __attribute__((always_inline));
	};
	
	
	template <std::size_t t_limit>
	struct inline_for_helper <0, t_limit>
	{
		template <typename t_fn>
		constexpr static void call(t_fn &&) __attribute__((always_inline)) {}
	};
	
	
	template <std::size_t t_i, std::size_t t_limit>
	template <typename t_fn>
	constexpr void inline_for_helper <t_i, t_limit>::call(t_fn &&fn)
	{
		fn(std::integral_constant <std::size_t, t_limit - t_i>());
		inline_for_helper <t_i - 1, t_limit>::call(fn);
	}
	
	
	template <typename t_type, std::size_t t_n, std::size_t t_i>
	struct array_fold_helper
	{
		template <typename t_fn>
		constexpr static inline t_type left_fold(std::array <t_type, t_n> const &arr, t_fn &&fn)
		{
			return fn(array_fold_helper <t_type, t_n, t_i - 1>::left_fold(arr, fn), std::get <t_i>(arr));
		}
	};
	
	
	// Partial specialization for the first array element.
	template <typename t_type, std::size_t t_n>
	struct array_fold_helper <t_type, t_n, 0>
	{
		template <typename t_fn>
		constexpr static inline t_type left_fold(std::array <t_type, t_n> const &arr, t_fn &&fn)
		{
			return std::get <0>(arr);
		}
	};
}


namespace founder_graphs {
	
	// Constant function.
	template <typename t_type>
	struct const_op
	{
		constexpr t_type operator()(t_type const orig, t_type const) const
		{
			return orig;
		}
	};
	
	
	template <typename t_ret>
	struct head_op
	{
		template <typename t_type>
		constexpr t_ret operator()(t_type &&val) const
		{
			return std::get <0>(val);
		}
	};
	
	
	// Get the first element of a parameter pack.
	template <typename t_first, typename ... t_rest>
	constexpr auto parameter_pack_head(t_first &&first, t_rest && ...)
	{
		return std::forward <t_first>(first);
	}
	
	
	template <std::size_t t_limit, typename t_fn>
	constexpr void inline_for(t_fn &&fn)
	{
		detail::inline_for_helper <t_limit, t_limit>::call(fn);
	}
	
	
	template <typename t_type, std::size_t t_n, typename t_fn>
	constexpr inline void array_apply(std::array <t_type, t_n> &arr, t_fn &&fn)
	{
		inline_for <t_n>([&](auto const idx){
			fn(std::get <idx()>(arr));
		});
	}
	
	
	template <typename t_type, std::size_t t_n, typename t_fn>
	constexpr inline t_type array_left_fold(std::array <t_type, t_n> const &arr, t_fn &&fn)
	{
		static_assert(0 < t_n, "The given array must be non-empty.");
		return detail::array_fold_helper <t_type, t_n, t_n - 1>::left_fold(arr, fn);
	}
	
	
	// FIXME: make this constexpr when if consteval is available.
	template <typename t_value_type>
	t_value_type max_value_for_bits(std::uint8_t const bits_)
	{
		constexpr std::uint8_t const value_type_bits(CHAR_BIT * sizeof(t_value_type));
		
		libbio_assert_lte(1, bits_);
		libbio_assert_lte(bits_, value_type_bits);
		
		// Don’t invoke undefined behaviour by shifting by CHAR_BIT * sizeof(t_value_type) or more.
		auto const bits(bits_ - 1);
		t_value_type retval(0x2);
		retval <<= bits;
		--retval;
		return retval;
	}
	
	
	template <typename t_int_vector>
	inline void push_back(t_int_vector &iv, typename t_int_vector::value_type const val)
	{
		libbio_assert_lte(val, max_value_for_bits <std::uint64_t>(iv.width()));
		iv.push_back(val);
	}


	template <typename t_int_vector>
	inline void assign_value(t_int_vector &iv, std::uint64_t const key, typename t_int_vector::value_type const val)
	{
		libbio_assert_lte(val, max_value_for_bits <std::uint64_t>(iv.width()));
		iv[key] = val;
	}
	
	
	std::tuple <std::size_t, std::size_t> check_file_size(libbio::file_handle const &handle);
	
	void read_from_file(libbio::file_handle const &handle, std::size_t const pos, std::size_t const read_count, char *buffer_start);
	
	
	template <typename t_archive>
	struct cereal_load_save_visitor
	{
		t_archive &archive;
		
		cereal_load_save_visitor(t_archive &archive_): archive(archive_) {}
		
		template <typename t_value>
		void operator()(std::string const &name, t_value &val) { archive(cereal::make_nvp(name, val)); }
	};
	
	
	struct sdsl_serialize_visitor
	{
		std::ostream				&os;
		sdsl::structure_tree_node	*node{};
		std::uint64_t				written_bytes{};
		
		sdsl_serialize_visitor(std::ostream &os_, sdsl::structure_tree_node *node_):
			os(os_),
			node(node_)
		{
		}
		
		template <typename t_value>
		void operator()(std::string const &name, t_value &val)
		{
			if constexpr (sdsl::has_serialize <t_value>::value)
				written_bytes += val.serialize(os, node, name);
			else
				written_bytes += sdsl::write_member(val, os, node, name);
		}
	};
	
	
	template <typename t_value>
	std::uint64_t sdsl_serialize(t_value &val, std::string const &name, sdsl::structure_tree_node *v, std::ostream &os)
	{
		sdsl::structure_tree_node *child(sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(val)));
		sdsl_serialize_visitor visitor(os, child);
		val.visit_members(val, visitor);
		sdsl::structure_tree::add_size(child, visitor.written_bytes);
		return visitor.written_bytes;
	}
}


namespace sdsl {
	
	template <typename t_int_vector>
	std::ostream &operator<<(std::ostream &os, int_vector_reference <t_int_vector> const &ref)
	{
		os << std::uint64_t(typename t_int_vector::value_type(ref));
		return os;
	}
}

#endif
