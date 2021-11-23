/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <catch2/catch.hpp>
#include <founder_graphs/segment_cmp.hh>
#include <fstream>

namespace fg	= founder_graphs;
namespace gen	= Catch::Generators;


namespace {
	
	typedef std::span <char const> span_type;
	
	
	std::string read_file_into_string(char const *path)
	{
		std::string retval;
		std::ifstream stream(path);
		
		stream.seekg(0, std::ios::end);
		retval.reserve(stream.tellg());
		stream.seekg(0, std::ios::beg);
		
		retval.assign(std::istreambuf_iterator <char>(stream), std::istreambuf_iterator <char>());
		return retval;
	}
	
	// In all cases only rhs is allowed to have gaps when calling strong_order()
	// due to how fg::segment_cmp has been implemented. operator() works in both cases.
	
	void test_equal_2(std::string const &lhs, span_type const rhs)
	{
		WHEN("the strings are compared")
		{
			fg::segment_cmp cmp;
			auto const res(cmp.strong_order(lhs, rhs));
			
			THEN("the result is “equal”")
			{
				CHECK(std::is_eq(res));
				CHECK(!cmp(lhs, rhs));
				CHECK(!cmp(rhs, lhs));
			}
		}
	}
	
	
	void test_equal(std::string const &lhs, std::string const &rhs)
	{
		span_type const rhs_(rhs.data(), rhs.size());
		test_equal_2(lhs, rhs_);
	}
	
	
	void test_lt_2(std::string const &lhs, span_type const rhs)
	{
		WHEN("the strings are compared")
		{
			fg::segment_cmp cmp;
			auto const res(cmp.strong_order(lhs, rhs));
			
			THEN("the result is “less than”")
			{
				CHECK(std::is_lt(res));
				CHECK(cmp(lhs, rhs));
				CHECK(!cmp(rhs, lhs));
			}
		}
	}
	
	
	void test_lt(std::string const &lhs, std::string const &rhs)
	{
		span_type const rhs_(rhs.data(), rhs.size());
		test_lt_2(lhs, rhs_);
	}
}


SCENARIO("segment_cmp can compare strings")
{
	GIVEN("a pair of equivalent strings")
	{
		test_equal("AAAA", "AAAA");
	}
	
	GIVEN("a pair of equivalent strings (2)")
	{
		test_equal("AAAA", "AAA-A");
	}
	
	GIVEN("a pair of equivalent strings (3)")
	{
		test_equal("AAAA", "AAAA-");
	}
	
	GIVEN("a pair of strings in increasing order (1)")
	{
		test_lt("AAAA", "AAAB");
	}
	
	GIVEN("a pair of strings in increasing order (2)")
	{
		test_lt("AAAA", "AAA-B");
	}
	
	GIVEN("a pair of strings in increasing order (3)")
	{
		test_lt("AAAA", "AAAB-");
	}
}
