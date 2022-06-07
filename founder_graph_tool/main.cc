/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cereal/archives/portable_binary.hpp>
#include <founder_graphs/founder_graph_indices/basic_types.hh>
#include <founder_graphs/founder_graph_indices/path_index.hh>
#include <iostream>
#include <libbio/algorithm.hh>
#include <libbio/assert.hh>
#include <libbio/file_handling.hh>
#include <range/v3/algorithm/copy.hpp>				// ranges::copy()
#include <range/v3/iterator/stream_iterators.hpp>	// ranges::make_ostream_joiner()
#include <sys/resource.h>							// getrusage()
#include "cmdline.h"

namespace ch	= std::chrono;
namespace fg	= founder_graphs;
namespace fgi	= founder_graphs::founder_graph_indices;
namespace lb	= libbio;


namespace {
	
	void load_index(char const *path, fgi::path_index &index)
	{
		lb::file_istream stream;
		lb::open_file_for_reading(path, stream);
		cereal::PortableBinaryInputArchive iarchive(stream);
		iarchive(index);
	}
	
	
	template <typename t_visitor>
	void visit_path_index_support(fgi::path_index_support_base const &lhs, fgi::path_index_support_base const &rhs, t_visitor &&visitor)
	{
		visitor("ℬ",           lhs.b,           rhs.b);
		visitor("ℰ",           lhs.e,           rhs.e);
		visitor("D",           lhs.d,           rhs.d);
		visitor("I",           lhs.i,           rhs.i);
		visitor("X",           lhs.x,           rhs.x);
		visitor("B",           lhs.bh,          rhs.bh);
		visitor("M",           lhs.m,           rhs.m);
		visitor("N",           lhs.n,           rhs.n);
		visitor("A",           lhs.a,           rhs.a);
		visitor("Ã",           lhs.a_tilde,     rhs.a_tilde);
		visitor("ℒ",           lhs.l,           rhs.l);
		visitor("ℛ",           lhs.r,           rhs.r);
		visitor("U",           lhs.u,           rhs.u);
		visitor("input_count", lhs.input_count, rhs.input_count);
		visitor("u_row_size",  lhs.u_row_size,  rhs.u_row_size);
	}
	
	
	template <typename t_value>
	struct value_writer
	{
		void operator()(t_value const &value, std::ostream &os) const
		{
			os << value;
		}
	};
	
	
	template <std::uint8_t t_bits>
	struct value_writer <sdsl::int_vector <t_bits>>
	{
		void operator()(sdsl::int_vector <t_bits> const &vec, std::ostream &os) const
		{
			std::uint64_t const count(vec.size());
			for (std::uint64_t i(0); i < count; ++i)
				os << '[' << i << "]: " << vec[i] << ' ';
			
		}
	};
	
	
	struct compare_visitor
	{
		template <typename t_value>
		void operator()(char const *name, t_value const &lhs, t_value const &rhs)
		{
			if (lhs != rhs)
			{
				value_writer <t_value> writer;
					
				std::cout << "Values for " << name << " differ.\n";
				std::cout << "lhs: ";
				writer(lhs, std::cout);
				std::cout << '\n';
				
				std::cout << "rhs: ";
				writer(rhs, std::cout);
				std::cout << '\n';
			}
		}
	};
	
	
	void compare_indices(char const *lhs_index_path, char const *rhs_index_path)
	{
		fgi::path_index lhs_index;
		fgi::path_index rhs_index;
		
		load_index(lhs_index_path, lhs_index);
		load_index(rhs_index_path, rhs_index);
		
		compare_visitor visitor;
		visit_path_index_support(lhs_index.get_support(), rhs_index.get_support(), visitor);
	}


	void describe_founder_graph(char const *index_path)
	{
		fgi::path_index index;
		load_index(index_path, index);
		
		auto const &index_support(index.get_support());
		auto const node_count(index_support.bh_rank1_support(index_support.bh.size()));
		auto const input_count(index_support.input_count);
		auto const u_row_size(index_support.u_row_size);
		
		// FIXME: not very efficient.
		if (input_count)
		{
			for (std::size_t i(0); i < node_count; ++i)
			{
				auto const u_pos(i * u_row_size);

				std::cout << "Node: " << i << " u_pos: " << u_pos << " Paths:";
				
				std::size_t remaining(0);
				std::uint64_t word{};
				for (std::size_t j(0); j < input_count; ++j)
				{
					if (0 == remaining)
					{
						auto const read_count(lb::min_ct(fgi::path_index_support_base::U_BV_BLOCK_SIZE, input_count - j));
						word = index_support.u.get_int(u_pos + j, read_count);
						remaining = read_count;
					}
					
					if (std::uint64_t(0x1) & word)
						std::cout << ' ' << j;
					
					word >>= 1;
					--remaining;
				}
				
				std::cout << '\n';
			}
		}
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
	
	if (args_info.compare_founder_graphs_given)
		compare_indices(args_info.index_arg, args_info.rhs_index_arg);
	else if (args_info.describe_given)
		describe_founder_graph(args_info.index_arg);
	else
	{
		std::cerr << "Unknown mode given.\n";
		return EXIT_FAILURE;
	}
	
	return EXIT_SUCCESS;
}
