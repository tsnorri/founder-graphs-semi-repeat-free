/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <charconv>
#include <founder_graphs/founder_graph_indices/block_graph.hh>
#include <iostream>
#include <libbio/utility.hh>
#include "cmdline.h"


namespace fg	= founder_graphs;
namespace fgi	= founder_graphs::founder_graph_indices;
namespace fs	= std::filesystem;
namespace lb	= libbio;


namespace {
	
	std::size_t read_next_block_number(std::size_t const limit)
	{
		std::string buffer;
		
		while (true)
		{
			std::cout << "Block number [0, " << limit << ")? " << std::flush;
			std::cin >> buffer;
			
			if (std::cin.eof())
				return SIZE_MAX;
			
			std::size_t block_number{};
			auto const res(std::from_chars(buffer.data(), buffer.data() + buffer.size(), block_number));
			if (std::errc{} != res.ec)
				continue;
			
			if (! (block_number < limit))
				continue;
			
			return block_number;
		}
	}
}


int main(int argc, char **argv)
{
	gengetopt_args_info args_info;
	if (0 != cmdline_parser(argc, argv, &args_info))
		std::exit(EXIT_FAILURE);
	
	// Build an uncompressed founder graph.
	lb::log_time(std::cerr) << "Loading the segmentation…\n";
	fgi::block_graph gr;
	fgi::read_optimized_segmentation(
		args_info.sequence_list_arg,
		args_info.segmentation_arg,
		args_info.bgzip_input_flag,
		gr
	);
	
	std::cout
		<< "Nodes:                   " << gr.node_count << '\n'
		<< "Edges:                   " << gr.edge_count << '\n'
		<< "Total node label length: " << gr.node_label_length_sum << '\n'
		<< "Max. node label length:  " << gr.node_label_max_length << '\n'
		<< "Aligned size:            " << gr.aligned_size << '\n'
		<< "Input count:             " << gr.input_count << '\n'
		<< "Max. block height:       " << gr.max_block_height << '\n';
	
	if (std::cin.eof())
		return EXIT_SUCCESS;
	
	while (true)
	{
		std::size_t block_idx(read_next_block_number(gr.blocks.size()));
		if (SIZE_MAX == block_idx)
			break;
		
		auto const &block(gr.blocks[block_idx]);
		
		std::cout << "Block " << block_idx << ":\n";
		std::cout << "Aligned pos:            " << block.aligned_position << '\n';
		std::cout << "Node csum:              " << block.node_csum << '\n';
		std::cout << "Node label length csum: " << block.node_label_length_csum << '\n';
		std::cout << "In-edges:\n";
		for (auto const &kv : block.reverse_in_edges)
			std::cout << '\t' << kv.second << " -> " << kv.first << '\n';
		std::cout << "Inputs:\n";
		for (auto const &kv : block.inputs)
			std::cout << '\t' << kv.first << " -> " << kv.second << '\n';
		std::cout << "Segments:\n";
		for (auto const &seg : block.segments)
			std::cout << "\t(" << seg.size() << ") " << seg << '\n';
	}
	
	return EXIT_SUCCESS;
}
