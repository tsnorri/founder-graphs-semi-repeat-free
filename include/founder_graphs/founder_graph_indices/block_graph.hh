/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_FOUNDER_GRAPH_INDICES_BLOCK_GRAPH_HH
#define FOUNDER_GRAPHS_FOUNDER_GRAPH_INDICES_BLOCK_GRAPH_HH

#include <founder_graphs/founder_graph_indices/basic_types.hh>
#include <map>
#include <set>
#include <string>
#include <vector>


namespace founder_graphs::founder_graph_indices {
	
	typedef std::map <std::string, count_type>		segment_map;	// Segments to segment numbers within a block.
	typedef std::vector <std::string>				segment_vector;	// ”
	typedef std::multimap <count_type, count_type>	input_map;		// Segment numbers to input numbers.
	typedef std::vector <count_type>				count_vector;
	typedef std::set <pair <count_type>>			edge_set;
	
	
	struct block
	{
		segment_vector	segments;					// Segments in lexicographic order.
		input_map		inputs;	
		edge_set		reverse_in_edges;			// In-edges s.t. the left item is the node number in this block.
		std::size_t		aligned_position{};			// Leftmost zero-based aligned position.
		std::size_t		node_csum{};				// Cumulative sum of nodes, not taking this block into account.
		std::size_t		node_label_length_csum{};	// Cumulative sum of node label lengths, not taking this block into account.
	};
	
	typedef std::vector <block>	block_vector_type;
	
	
	struct block_graph
	{
		block_vector_type	blocks; 					// The last block is a sentinel with no segments, inputs or in-edges.
		std::size_t			node_count{};
		std::size_t			edge_count{};
		std::size_t			node_label_length_sum{};
		std::size_t			node_label_max_length{};
		std::size_t			aligned_size{};
		count_type			input_count{};
		count_type			max_block_height{};
		
		void reset();
		std::size_t first_block_segment_count() const { return blocks.front().segments.size(); }
	};
	
	
	// Postcondition: gr reflects the contents of the other parameters.
	void read_optimized_segmentation(
		char const *sequence_list_path,
		char const *segmentation_path,
		bool const input_is_bgzipped,
		block_graph &gr
	);
	
	
	struct indexable_sequence_output_delegate
	{
		virtual ~indexable_sequence_output_delegate() {}
		
		virtual void output_segment(
			std::size_t const block_idx,
			std::size_t const file_offset,
			std::size_t const seg_idx,
			std::size_t const seg_size
		) {}
		
		virtual void output_edge(
			std::size_t const block_idx,
			std::size_t const file_offset,
			std::size_t const lhs_seg_idx,
			std::size_t const rhs_seg_idx,
			std::size_t const lhs_seg_size,
			std::size_t const rhs_seg_size
		) {}
		
		virtual void finish() {}
	};
	
	
	// Postcondition: an indexable sequence has been written to stream.
	void write_indexable_sequence(block_graph const &gr, std::ostream &stream, indexable_sequence_output_delegate &delegate);
	
	inline void write_indexable_sequence(block_graph const &gr, std::ostream &stream)
	{
		indexable_sequence_output_delegate delegate;
		write_indexable_sequence(gr, stream, delegate);
	}
	
	// Postcondition: the graph has been written to stream in Graphviz format.
	void write_graphviz(block_graph const &gr, std::ostream &stream);
}

#endif
