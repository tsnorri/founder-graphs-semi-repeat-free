/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <catch2/catch.hpp>
#include <founder_graphs/sort.hh>
#include <libbio/assert.hh>
#include <range/v3/view/enumerate.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include "rapidcheck_additions.hh"


namespace fg	= founder_graphs;
namespace gen	= Catch::Generators;
namespace rsv	= ranges::views;


namespace {

	template <std::uint8_t t_width>
	struct int_vector_helper
	{
		sdsl::int_vector <t_width> value;
		
		int_vector_helper(std::vector <std::uint64_t> const &elements_):
			value(elements_.size(), 0)
		{
			for (auto const &[idx, val] : rsv::enumerate(elements_))
				value[idx] = val;
		}
	};
	
	
	template <>
	struct int_vector_helper <0>
	{
		sdsl::int_vector <0> value;
		
		int_vector_helper(std::vector <std::uint64_t> const &elements_, std::uint8_t const width_):
			value(elements_.size(), 0, width_)
		{
			for (auto const &[idx, val] : rsv::enumerate(elements_))
				value[idx] = val;
		}
	};


	template <std::uint8_t t_width>
	std::ostream &operator<<(std::ostream &os, int_vector_helper <t_width> const &helper)
	{
		if constexpr (0 == t_width)
			os << "width: " << +helper.value.width() << ' ';
			
		os << "elements:";
		for (auto const val : helper.value)
			os << ' ' << +val;
		
		return os;
	}


	template <typename t_vector>
	void test_sort(t_vector &vec)
	{
		WHEN("founder_graphs::sort() is called")
		{
			fg::sort(vec.begin(), vec.end());
			THEN("the vector becomes sorted")
			{
				REQUIRE(std::is_sorted(vec.begin(), vec.end()));
			}
		}
	}


	template <typename t_type>
	t_type make_small_vector(std::size_t const size)
	{
		return t_type(size, 0);
	}


	template <>
	sdsl::int_vector <0> make_small_vector(std::size_t const size)
	{
		return sdsl::int_vector <0>(size, 0, 6); // Use 6 bits per element for now.
	}
}


namespace rc {
	
	// FIXME: try to use the collection building classes?
	
	template <std::uint8_t t_width>
	struct Arbitrary <int_vector_helper <t_width>>
	{
		static_assert(0 < t_width);
		static_assert(t_width <= 64);
		
		static Gen <int_vector_helper <t_width>> arbitrary()
		{
			return gen::withSize([](int const size){
				constexpr auto const max_val((std::uint64_t(0x2) << (t_width - 1)) - 1);
				return gen::construct <int_vector_helper <t_width>>(
					gen::container <std::vector <std::uint64_t>>(
						size,
						gen::inClosedRange(std::uint64_t(0), max_val)
					)
				);
			});
		}
	};
	
	
	template <>
	struct Arbitrary <int_vector_helper <0>>
	{
		static Gen <int_vector_helper <0>> arbitrary()
		{
			return gen::withSize([](int const size){
				return gen::mapcat(gen::inClosedRange(1, 64), [size](std::uint8_t const width){
					libbio_assert_lt(0, width);
					libbio_assert_lte(width, 64);
					auto const max_val((std::uint64_t(0x2) << (width - 1)) - 1);
					return gen::construct <int_vector_helper <0>>(
						gen::container <std::vector <std::uint64_t>>(
							size,
							gen::inClosedRange(std::uint64_t(0), max_val)
						),
						gen::just(width)
					);
				});
			});
		}
	};
}


TEMPLATE_TEST_CASE(
	"founder_graphs::sort() works on a simple input", "[template][sort]",
	std::vector <std::uint8_t>,
	sdsl::int_vector <8>,
	sdsl::int_vector <0>
) {
	GIVEN("A small vector (1)") {
		auto vec(make_small_vector <TestType>(5));
		vec.assign({2, 1, 3, 5, 4});
		test_sort(vec);
	}
	
	GIVEN("A small vector (2)") {
		auto vec(make_small_vector <TestType>(6));
		vec.assign({2, 1, 2, 3, 5, 4});
		test_sort(vec);
	}
	
	GIVEN("A small vector (3)") {
		auto vec(make_small_vector <TestType>(5));
		vec.assign({4, 3, 0, 1, 2});
		test_sort(vec);
	}
	
	GIVEN("A small vector (4)") {
		auto vec(make_small_vector <TestType>(5));
		vec.assign({2, 2, 0, 1, 1});
		test_sort(vec);
	}
}


TEMPLATE_TEST_CASE(
	"founder_graphs::sort() produces a sorted vector", "[sort][template]",
	int_vector_helper <8>,
	int_vector_helper <16>,
	int_vector_helper <32>,
	int_vector_helper <64>,
	int_vector_helper <0>
)
{
	rc::prop("Calling fg::sort() on an unsorted vector results in a sorted vector ", [](TestType helper){ // Copy.
		auto &vec(helper.value);
		fg::sort(vec.begin(), vec.end());
		return std::is_sorted(vec.begin(), vec.end());
	}, true);
	
	rc::prop("Calling fg::sort() on a sorted vector results in a sorted vector ", [](TestType helper){ // Copy.
		auto &vec(helper.value);
		std::sort(vec.begin(), vec.end());
		REQUIRE(std::is_sorted(vec.begin(), vec.end()));
		fg::sort(vec.begin(), vec.end());
		return std::is_sorted(vec.begin(), vec.end());
	}, true);
}
