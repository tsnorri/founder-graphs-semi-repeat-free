/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <algorithm>
#include <founder_graphs/founder_graph_indices/block_graph.hh>
#include <founder_graphs/msa_reader.hh>
#include <libbio/file_handling.hh>
#include <range/v3/algorithm/copy.hpp>
#include <range/v3/algorithm/fill.hpp>
#include <range/v3/iterator/insert_iterators.hpp>
#include <range/v3/numeric/accumulate.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/tail.hpp>

namespace fg		= founder_graphs;
namespace fgi		= founder_graphs::founder_graph_indices;
namespace lb		= libbio;
namespace rsv		= ranges::views;


namespace {
	
	// For assigning std::span <char> to std::string.
	void remove_gaps_and_assign(std::span <char> const src, std::string &dst)
	{
		dst.resize(src.size());
		auto const res(std::copy_if(src.begin(), src.end(), dst.begin(), [](auto const cc){
			return '-' != cc;
		}));
		dst.resize(res - dst.begin());
	}
	
	
	void read_block(
		fg::msa_reader &reader,
		fg::length_type const lb,
		fg::length_type const rb,
		fgi::segment_map &segments,
		fgi::input_map &inputs,
		fgi::count_vector &inv_inputs,
		fgi::count_vector &permutation
	)
	{
		segments.clear();
		inputs.clear();
		permutation.clear();
		ranges::fill(inv_inputs, fg::LENGTH_MAX); // For extra safety.
		
		// Assign initial segment numbers.
		reader.fill_buffer(
			lb,
			rb,
			[&segments, &inputs, &inv_inputs](fg::msa_reader::span_vector const &spans){
				std::size_t segment_counter{};
				std::string key_buffer{};
				
				for (auto const &[input_idx, seg_span] : rsv::enumerate(spans))
				{
					// Add the segment to the map if needed.
					key_buffer.clear();
					remove_gaps_and_assign(seg_span, key_buffer);
					auto res(segments.try_emplace(std::move(key_buffer), 0));
					auto &seg_idx(res.first->second);
					if (res.second) // did emplace.
						seg_idx = segment_counter++;
					
					// Associate the segment with the input.
					inv_inputs[input_idx] = seg_idx;
				}
				
				return true;
			}
		);
		
		// Permute the identifiers s.t. they are in the lexicographic order of the segments.
		permutation.resize(segments.size(), 0);
		for (auto &&[idx, kv] : rsv::enumerate(segments))
		{
			permutation[kv.second] = idx;
			kv.second = idx;
		}
		
		// Permute and update inputs.
		for (auto &&[input_idx, seg_idx] : rsv::enumerate(inv_inputs))
		{
			seg_idx = permutation[seg_idx];
			inputs.emplace(seg_idx, input_idx);
		}
	}
	
	
	void update_graph_first_block(fgi::segment_map const &segments, fgi::input_map const &inputs, fgi::block_graph &gr)
	{
		auto &block(gr.blocks.emplace_back());
		ranges::copy(rsv::keys(segments), ranges::back_inserter(block.segments));
		block.inputs = inputs;
		
		// No in-edges in the first block.
		// aligned_position is zero.
		// node_csum is zero. (Before this block only.)
		// node_label_length_csum is zero. (Before this block only.)
		
		auto const fold_res(ranges::accumulate(rsv::keys(segments), fg::pair <std::size_t>(0, 0), [](auto acc, auto const &seg){
			acc.first += seg.size();						// Sum of the lengths.
			acc.second = std::max(acc.second, seg.size());	// Max. length.
			return acc;
		}));
		
		gr.node_count += segments.size();
		gr.node_label_length_sum += fold_res.first;
		gr.node_label_max_length = std::max(gr.node_label_max_length, fold_res.second);
		gr.max_block_height = std::max(gr.max_block_height, fg::count_type(segments.size()));
		// No in-edges in the first block.
	}


	void update_graph(
		std::size_t const aln_pos,
		fgi::segment_map const &segments,
		fgi::input_map const &inputs,
		fgi::edge_set const &reverse_edges,
		fgi::block_graph &gr
	)
	{
		auto &block(gr.blocks.emplace_back());
		ranges::copy(rsv::keys(segments), ranges::back_inserter(block.segments));
		block.inputs = inputs;
		block.reverse_in_edges = reverse_edges;
		
		block.aligned_position = aln_pos;
		block.node_csum = gr.node_count;
		block.node_label_length_csum = gr.node_label_length_sum;
		
		auto const fold_res(ranges::accumulate(rsv::keys(segments), fg::pair <std::size_t>(0, 0), [](auto acc, auto const &seg){
			acc.first += seg.size();						// Sum of the lengths.
			acc.second = std::max(acc.second, seg.size());	// Max. length.
			return acc;
		}));
		
		gr.node_count += segments.size();
		gr.node_label_length_sum += fold_res.first;
		gr.node_label_max_length = std::max(gr.node_label_max_length, fold_res.second);
		gr.max_block_height = std::max(gr.max_block_height, fg::count_type(segments.size()));
		gr.edge_count += reverse_edges.size();
	}
	
	
	void read_optimized_segmentation_(
		fg::msa_reader &reader,
		char const *sequence_list_path,
		char const *segmentation_path,
		fgi::block_graph &gr
	)
	{
		gr.reset();
		
		lb::file_istream segmentation_stream;
		lb::open_file_for_reading(segmentation_path, segmentation_stream);
		cereal::PortableBinaryInputArchive iarchive(segmentation_stream);
		
		// Read the sequence file paths.
		{
			lb::file_istream sequence_list_stream;;
			lb::open_file_for_reading(sequence_list_path, sequence_list_stream);
			
			std::string path;
			while (std::getline(sequence_list_stream, path))
				reader.add_file(path);
		}
		
		reader.prepare();
		
		// Read the block boundaries.
		fg::length_type block_count{}; // The count is stored as a length_type.
		iarchive(cereal::make_size_tag(block_count));
		
		// Process the blocks.
		if (block_count)
		{
			auto const seq_count(reader.handle_count());
		
			fgi::segment_map lhs_segments;					// segment -> segment number within a block
			fgi::segment_map rhs_segments;
			fgi::input_map lhs_inputs;						// segment number within a block -> input number
			fgi::input_map rhs_inputs;
			fgi::count_vector inv_lhs_inputs(seq_count, 0);	// input number -> segment number within a block
			fgi::count_vector inv_rhs_inputs(seq_count, 0);
			fgi::count_vector permutation;					// Buffer.
			fgi::edge_set reverse_edges;					// Edge in rhs block -> edge in lhs block.
			
			fg::length_type lb{};
			fg::length_type rb{};
			iarchive(rb);
			
			// Update the graph.
			gr.blocks.reserve(1 + block_count);
			gr.input_count = seq_count;
			gr.aligned_size = reader.aligned_size();
			
			// Process the first block.
			read_block(reader, lb, rb, lhs_segments, lhs_inputs, inv_lhs_inputs, permutation);
			update_graph_first_block(lhs_segments, lhs_inputs, gr);
			
			lb = rb;
			for (fg::count_type i(1); i < block_count; ++i)
			{
				if (0 == i % 100000)
					lb::log_time(std::cerr) << "Block " << i << '/' << block_count << "…\n";
				
				// Read the next right bound.
				iarchive(rb);
				
				// Process the block.
				read_block(reader, lb, rb, rhs_segments, rhs_inputs, inv_rhs_inputs, permutation);
				
				// Update the edge list.
				reverse_edges.clear();
				for (auto const &[lhs, rhs] : rsv::zip(inv_lhs_inputs, inv_rhs_inputs))
					reverse_edges.emplace(rhs, lhs);
				
				update_graph(lb, rhs_segments, rhs_inputs, reverse_edges, gr);
				
				using std::swap;
				swap(lhs_segments, rhs_segments);
				swap(lhs_inputs, rhs_inputs);
				swap(inv_lhs_inputs, inv_rhs_inputs);
				lb = rb;
			}
		}
		
		// Add a sentinel block.
		{
			auto &sentinel_block(gr.blocks.emplace_back());
			sentinel_block.aligned_position = gr.aligned_size;
			sentinel_block.node_csum = gr.node_count;
			sentinel_block.node_label_length_csum = gr.node_label_length_sum;
		}
	}
	
	
	// Graphviz output helpers.
	struct gv_node_id
	{
		std::size_t block_idx{};
		std::size_t node_idx{};
		
		gv_node_id(std::size_t const block_idx_, std::size_t const node_idx_):
			block_idx(block_idx_),
			node_idx(node_idx_)
		{
		}
	};
	
	std::ostream &operator<<(std::ostream &os, gv_node_id const &nl)
	{
		os << '_' << nl.block_idx << '_' << nl.node_idx;
		return os;
	}
	
	struct escape_gv
	{
		std::string const &text;
		
		escape_gv(std::string const &text_):
			text(text_)
		{
		}
	};
	
	std::ostream &operator<<(std::ostream &os, escape_gv const &eg)
	{
		for (auto const cc : eg.text)
		{
			if ('"' == cc)
				os << "\\\"";
			else
				os << cc;
		}
		return os;
	}
	
	
	void write_segments_gv(fgi::block const &block, std::size_t const block_idx, std::ostream &stream)
	{
		for (auto const &[node_idx, seg] : rsv::enumerate(block.segments))
			stream << '\t' << gv_node_id(block_idx, node_idx) << ' ' << "[label = \"" << escape_gv(seg) << "\"]\n";
	}
	
	
	void write_edges_gv(fgi::block const &block, std::size_t const block_idx, std::ostream &stream)
	{
		for (auto const &[rhs, lhs] : block.reverse_in_edges)
			stream << '\t' << gv_node_id(block_idx - 1, lhs) << " -> " << gv_node_id(block_idx, rhs) << '\n';
	}
}


namespace founder_graphs::founder_graph_indices {
	
	void block_graph::reset()
	{
		blocks.clear();
		node_count = 0;
		edge_count = 0;
		node_label_length_sum = 0;
		aligned_size = 0;
		input_count = 0;
		max_block_height = 0;
	}
	
	
	void read_optimized_segmentation(
		char const *sequence_list_path,
		char const *segmentation_path,
		bool const input_is_bgzipped,
		block_graph &gr
	)
	{
		if (input_is_bgzipped)
		{
			bgzip_msa_reader reader;
			read_optimized_segmentation_(reader, sequence_list_path, segmentation_path, gr);
		}
		else
		{
			text_msa_reader reader;
			read_optimized_segmentation_(reader, sequence_list_path, segmentation_path, gr);
		}
	}
	
	
	void write_indexable_sequence(block_graph const &gr, std::ostream &os, indexable_sequence_output_delegate &delegate)
	{
		os << '#';
		
		auto const block_count(gr.blocks.size());
		if (block_count)
		{
			// Add segments in the first block terminated with #.
			for (auto const &[idx, seg] : rsv::enumerate(gr.blocks.front().segments))
			{
				delegate.output_segment(0, os.tellp(), idx, seg.size());
				os << seg << '#';
			}
			
			// Rest of the edges.
			for (std::size_t i(1); i < block_count; ++i)
			{
				auto const &lhs_block(gr.blocks[i - 1]);
				auto const &rhs_block(gr.blocks[i]);
				auto const &lhs_segments(lhs_block.segments);
				auto const &rhs_segments(rhs_block.segments);
				
				for (auto const &edge : rhs_block.reverse_in_edges)
				{
					auto const rhs_idx(edge.first);
					auto const lhs_idx(edge.second);
					auto const &lhs_seg(lhs_segments[lhs_idx]);
					auto const &rhs_seg(rhs_segments[rhs_idx]);
					delegate.output_edge(i, os.tellp(), lhs_idx, rhs_idx, lhs_seg.size(), rhs_seg.size());
					os << lhs_seg;
					os << rhs_seg;
					os << '#';
				}
			}
		}
		
		os << std::flush;
		delegate.finish();
	}
	
	
	void write_graphviz(block_graph const &gr, std::ostream &stream)
	{
		stream << "digraph {\n";
		stream << "\trankdir=\"LR\"\n";
		
		auto const block_count(gr.blocks.size());
		if (1 < block_count)
		{
			auto const &first_block(gr.blocks.front());
			write_segments_gv(first_block, 0, stream);
			
			for (std::size_t i(1); i < block_count - 1; ++i) // Skip the sentinel.
			{
				auto const &block(gr.blocks[i]);
				write_segments_gv(block, i, stream);
				write_edges_gv(block, i, stream);
			}
		}
		
		stream << "}\n";
		stream << std::flush;
	}
}
