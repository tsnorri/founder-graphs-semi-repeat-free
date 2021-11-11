/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <catch2/catch.hpp>
#include <founder_graphs/count_distinct.hh>

namespace fg	= founder_graphs;
namespace gen	= Catch::Generators;


namespace {
	void run_test(std::vector <std::size_t> const &vec, std::size_t const expected_count)
	{
		WHEN("the values are counted")
		{
			auto const count(fg::count_distinct(vec.begin(), vec.end()));
			THEN("the count equals the expected")
			{
				CHECK(count == expected_count);
			}
		}
	}
}


SCENARIO("count_distinct can count distinct elements in a vector")
{
	GIVEN("an empty vector")
	{
		std::vector <std::size_t> const vec{};
		run_test(vec, 0);
	}

	GIVEN("a vector with one element")
	{
		std::vector <std::size_t> const vec{1};
		run_test(vec, 1);
	}

	GIVEN("a vector (1)")
	{
		std::vector <std::size_t> const vec{1, 2, 3, 4, 5};
		run_test(vec, 5);
	}

	GIVEN("a vector (2)")
	{
		std::vector <std::size_t> const vec{1, 1, 2, 3, 4, 5};
		run_test(vec, 5);
	}

	GIVEN("a vector (3)")
	{
		std::vector <std::size_t> const vec{1, 2, 3, 4, 5, 5};
		run_test(vec, 5);
	}

	GIVEN("a vector (4)")
	{
		std::vector <std::size_t> const vec{1, 2, 3, 3, 4, 5};
		run_test(vec, 5);
	}

	GIVEN("a vector (5)")
	{
		std::vector <std::size_t> const vec{1, 1, 2, 3, 3, 4, 5, 5};
		run_test(vec, 5);
	}

	GIVEN("a vector (6)")
	{
		std::vector <std::size_t> const vec{1, 1, 2, 3, 3, 4, 4, 5, 5};
		run_test(vec, 5);
	}

	GIVEN("a vector (7)")
	{
		std::vector <std::size_t> const vec{1, 2, 2};
		run_test(vec, 2);
	}

	GIVEN("a vector (8)")
	{
		std::vector <std::size_t> const vec{1, 1, 2};
		run_test(vec, 2);
	}

	GIVEN("a vector (9)")
	{
		std::vector <std::size_t> const vec{1, 1, 1};
		run_test(vec, 1);
	}

	GIVEN("a vector (10)")
	{
		std::vector <std::size_t> const vec{1, 2};
		run_test(vec, 2);
	}

	GIVEN("a vector (11)")
	{
		std::vector <std::size_t> const vec{1, 1};
		run_test(vec, 1);
	}
}
