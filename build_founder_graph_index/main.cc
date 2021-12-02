/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/string.hpp>
#include <compare>
#include <founder_graphs/basic_types.hh>
#include <founder_graphs/founder_graph_index.hh>
#include <founder_graphs/msa_reader.hh>
#include <founder_graphs/segment_cmp.hh>
#include <iostream>
#include <libbio/file_handling.hh>
#include <libbio/utility.hh>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/reverse.hpp>
#include <set>
#include <string>
#include "cmdline.h"

namespace fg	= founder_graphs;
namespace lb	= libbio;
namespace rsv	= ranges::views;


namespace {
	
	struct segment_properties
	{
		std::size_t segment_number{SIZE_MAX};
		std::size_t count{};
		std::size_t prefixes{};
		std::size_t edges{};
		
		segment_properties() = default;
		
		segment_properties(
			std::size_t const segment_number_,
			std::size_t const count_,
			std::size_t const prefixes_,
			std::size_t const edges_
		):
			segment_number(segment_number_),
			count(count_),
			prefixes(prefixes_),
			edges(edges_)
		{
		}
	};
	
	
	typedef std::map <std::string, segment_properties, fg::segment_cmp>			segment_map;
	typedef std::multimap <std::string, segment_properties, fg::segment_cmp>	segment_buffer_type;
	
	
	class output_handler
	{
	protected:
		bool m_should_skip_output{};

	public:
		output_handler(bool const should_skip_output):
			m_should_skip_output(should_skip_output)
		{
		}

		virtual ~output_handler() {}
		virtual bool needs_properties() const = 0;
		virtual void output_header(fg::length_type const block_count) {}
		virtual void output(std::size_t const block_idx, segment_map const &segments) = 0;
		virtual void finish() {}
	};
	
	
	class indexable_text_output_handler final : public output_handler
	{
	protected:
		bool m_should_skip_trailing_zero{};

	public:
		indexable_text_output_handler(bool const should_skip_trailing_zero, bool const should_skip_output):
			output_handler(should_skip_output),
			m_should_skip_trailing_zero(should_skip_trailing_zero)
		{
		}

		bool needs_properties() const override { return false; }
		
		void output(std::size_t const block_idx, segment_map const &segments) override
		{
			if (m_should_skip_output)
				return;
			
			// Output.
			for (auto const &kv : segments)
			{
				std::cout << '#';
				std::copy(kv.first.crbegin(), kv.first.crend(), std::ostream_iterator <char>(std::cout));
			}
		}

		void finish() override
		{
			std::cout << '\0';
		}
	};
	
	
	class block_content_output_handler : public output_handler
	{
	protected:
		bool m_should_omit_segments{};
		
	public:
		block_content_output_handler(bool const should_omit_segments, bool const should_skip_output):
			output_handler(should_skip_output),
			m_should_omit_segments(should_omit_segments)
		{
		}
		
		bool needs_properties() const final { return true; }
	};
	
	
	class block_content_tsv_output_handler final : public block_content_output_handler
	{
	public:
		using block_content_output_handler::block_content_output_handler;
		
		void output_header(fg::length_type const block_count) override
		{
			if (m_should_skip_output)
				return;

			// Output segments one per line with prefix counts.
			if (m_should_omit_segments)
				std::cout << "BLOCK_INDEX\tPREFIX_COUNT\tEDGE_COUNT\tSEGMENT_LENGTH\n";
			else
				std::cout << "BLOCK_INDEX\tPREFIX_COUNT\tEDGE_COUNT\tSEGMENT\n";
		}
		
		void output(std::size_t const block_idx, segment_map const &segments) override
		{
			if (m_should_skip_output)
				return;

			if (m_should_omit_segments)
			{
				for (auto const &kv : segments)
					std::cout << block_idx << '\t' << kv.second.prefixes << '\t' << kv.second.edges << '\t' << kv.first.size() << '\n';
			}
			else
			{
				for (auto const &kv : segments)
					std::cout << block_idx << '\t' << kv.second.prefixes << '\t' << kv.second.edges << '\t' << kv.first << '\n';
			}
		}
	};
	
	
	class block_content_binary_output_handler final : public block_content_output_handler
	{
	protected:
		cereal::PortableBinaryOutputArchive m_archive{std::cout};
			
	public:
		using block_content_output_handler::block_content_output_handler;
		
		void output_header(fg::length_type const block_count) override
		{
			if (m_should_skip_output)
				return;

			m_archive(cereal::make_size_tag(block_count));
		}
		
		void output(std::size_t const block_idx, segment_map const &segments) override
		{
			if (m_should_skip_output)
				return;

			fg::length_type segment_count(segments.size());
			m_archive(cereal::make_size_tag(segment_count));
			
			if (m_should_omit_segments)
			{
				for (auto const &kv : segments)
				{
					fg::length_type prefix_count(kv.second.prefixes);
					fg::length_type edge_count(kv.second.edges);
					fg::length_type segment_length(kv.first.size());
					m_archive(prefix_count);
					m_archive(edge_count);
					m_archive(segment_length);
				}
			}
			else
			{
				for (auto const &kv : segments)
				{
					fg::length_type prefix_count(kv.second.prefixes);
					fg::length_type edge_count(kv.second.edges);
					m_archive(prefix_count);
					m_archive(edge_count);
					m_archive(kv.first);
				}
			}
		}
	};


	void read_block_contents(char const *block_contents_path, bool const should_read_segments)
	{
		lb::file_istream stream;
		lb::open_file_for_reading(block_contents_path, stream);
		cereal::PortableBinaryInputArchive archive(stream);
		
		// Read the header.
		fg::length_type block_count{};
		archive(cereal::make_size_tag(block_count));
		
		if (should_read_segments)
		{
			std::cout << "BLOCK\tPREFIX_COUNT\tEDGE_COUNT\tSEGMENT\n";
			
			std::string segment;
			for (fg::length_type j(0); j < block_count; ++j)
			{
				// Read the segment count.
				fg::length_type segment_count{};
				archive(cereal::make_size_tag(segment_count));
				
				for (fg::length_type i(0); i < segment_count; ++i)
				{
					segment.clear();
					
					fg::length_type prefix_count{};
					fg::length_type edge_count{};
					archive(prefix_count);
					archive(edge_count);
					archive(segment);
					std::cout << j << '\t' << prefix_count << '\t' << edge_count << '\t' << segment << '\n';
				}
			}
		}
		else
		{
			std::cout << "BLOCK\tPREFIX_COUNT\tEDGE_COUNT\tSEGMENT_LENGTH\n";

			for (fg::length_type j(0); j < block_count; ++j)
			{
				// Read the segment count.
				fg::length_type segment_count{};
				archive(cereal::make_size_tag(segment_count));
				
				for (fg::length_type i(0); i < segment_count; ++i)
				{
					fg::length_type prefix_count{};
					fg::length_type edge_count{};
					fg::length_type segment_length{};
					std::cout << j << '\t' << prefix_count << '\t' << edge_count << '\t' << segment_length << '\n';
				}
			}
		}
	}
	
	
	// For assigning std::span <char> to std::string.
	void remove_gaps_and_assign(std::span <char> const src, std::string &dst)
	{
		dst.resize(src.size());
		auto const res(std::copy_if(src.begin(), src.end(), dst.begin(), [](auto const cc){
			return '-' != cc;
		}));
		dst.resize(res - dst.begin());
	}


	std::string compare_for_debugging(std::span <char> const lhs_, std::string const &rhs_)
	{
		std::string_view lhs(lhs_.data(), lhs_.size());
		std::string_view rhs(rhs_);
		for (auto const &[i, tup] : rsv::enumerate(rsv::zip(lhs, rhs)))
		{
			auto const &[lhsc, rhsc] = tup;
			if (lhsc != rhsc)
			{
				if (10 < i)
					return boost::str(boost::format("“…%s” vs. “…%s”") % lhs.substr(i - 10, 11) % rhs.substr(i - 10, 11));
				else
					return boost::str(boost::format("“%s” vs. “%s”") % lhs.substr(0, 1 + i) % rhs.substr(0, 1 + i));
			}
		}

		return "(No differences found.)";
	}
	
	
	void handle_block_range(
		fg::msa_reader &reader,
		std::size_t const lb,
		std::size_t const rb,
		output_handler const &handler,
		segment_map &segments,
		segment_buffer_type &segment_buffer,
		std::vector <std::size_t> &segment_numbers_by_seq_idx,
		std::vector <segment_map::mapped_type *> &segments_by_segment_number
	)
	{
		// Determine the distinct segments in a block and number them.
		// The segments are placed in the segment map and their numbers
		// by sequence index in segment_numbers_by_seq_idx. The intention is that
		// the in-edge and out-edge counts can be determined from the output.
		
		// Move the nodes to the buffer.
		while (!segments.empty())
		{
			auto nh(segments.extract(segments.begin()));
			nh.key().clear();
			segment_buffer.insert(segment_buffer.end(), std::move(nh));
		}
		
		// Fill for extra safety.
		std::fill(segment_numbers_by_seq_idx.begin(), segment_numbers_by_seq_idx.end(), SIZE_MAX);
		
		// Currently fill_buffer() returns after the callback has finished.
		reader.fill_buffer(
			lb,
			rb,
			[
				&segments,
				&segment_buffer,
				&segment_numbers_by_seq_idx,
				&segments_by_segment_number
			](fg::msa_reader::span_vector const &spans){
				
				std::size_t segment_counter{};
				segments_by_segment_number.clear();
				
				libbio_assert_eq_msg(spans.size(), segment_numbers_by_seq_idx.size(), "spans.size(): ", spans.size(), " segment_numbers_by_seq_idx.size(): ", segment_numbers_by_seq_idx.size());
				for (auto &&[seq_idx, tup] : rsv::enumerate(rsv::zip(spans, segment_numbers_by_seq_idx)))
				{
					auto &[span, segment_number] = tup;
					
					// Check if this segment has already been handled.
					auto const it(segments.find(span));
					if (segments.end() == it)
					{
						// New segment, add to the set.
						if (segment_buffer.empty())
						{
							std::string key;
							remove_gaps_and_assign(span, key);
							auto const res(segments.emplace(
								std::piecewise_construct,
								std::forward_as_tuple(std::move(key)),
								std::forward_as_tuple(segment_counter, 1, 0, 0)
							));
							
							libbio_assert_msg(res.second, "Unable to insert text even though find() returned end(). Segment and found text: ", compare_for_debugging(span, res.first->first), "\nlhs: “", std::string_view(span.data(), span.size()), "”\nrhs: “", res.first->first, "”");
							auto &kv(*res.first);
							segments_by_segment_number.emplace_back(&kv.second);
						}
						else
						{
							auto nh(segment_buffer.extract(segment_buffer.begin()));
							remove_gaps_and_assign(span, nh.key());
							nh.mapped() = segment_properties(segment_counter, 1, 0, 0);
							auto const res(segments.insert(std::move(nh)));
							
							libbio_assert(res.inserted);
							auto &kv(*res.position);
							segments_by_segment_number.emplace_back(&kv.second);
						}
						
						segment_number = segment_counter;
						++segment_counter;
					}
					else
					{
						// Existing segment.
						auto &properties(it->second);
						++properties.count;
						segment_number = properties.segment_number;
					}
				}
				
				libbio_assert_eq(segment_counter, segments_by_segment_number.size());
				
				return true;
			}
		);
		
		if (handler.needs_properties())
		{
			// Count the prefixes.
			// Use a naïve algorithm since the amount of data is expected to be small,
			// i.e. just check if a segment is a prefix of the succeeding ones in the lexicographic order.
			auto it(segments.begin());
			auto const end(segments.end());
			while (it != end)
			{
				libbio_assert_eq(0, it->second.prefixes);
				auto next_it(std::next(it));
				while (next_it != end && next_it->first.starts_with(it->first))
				{
					++it->second.prefixes;
					++next_it;
				}
				++it;
			}
		}
	}
	
	
	void generate_indexable_text(
		fg::msa_reader &reader,
		char const *sequence_list_path,
		char const *segmentation_path,
		output_handler &handler,
		bool const should_output_block_contents
	)
	{
		// Open the segmentation.
		lb::log_time(std::cerr) << "Loading the segmentation…\n";
		lb::file_istream segmentation_stream;
		lb::open_file_for_reading(segmentation_path, segmentation_stream);
		cereal::PortableBinaryInputArchive archive(segmentation_stream);
		
		// Read the sequence file paths.
		{
			lb::log_time(std::cerr) << "Loading the sequences…\n";
			lb::file_istream sequence_list_stream;;
			lb::open_file_for_reading(sequence_list_path, sequence_list_stream);
			
			std::string path;
			while (std::getline(sequence_list_stream, path))
				reader.add_file(path);
		}
		
		reader.prepare();
		
		// Read the block boundaries and generate the text.
		lb::log_time(std::cerr) << "Processing…\n";
		fg::length_type block_count{};
		archive(cereal::make_size_tag(block_count));
		
		segment_buffer_type segment_buffer;
		
		handler.output_header(block_count);
		
		if (should_output_block_contents)
		{
			auto const seq_count(reader.handle_count());
			segment_map lhs_segments;
			segment_map rhs_segments;
			std::vector <std::size_t> lhs_segment_numbers_by_seq_idx(seq_count, SIZE_MAX);
			std::vector <std::size_t> rhs_segment_numbers_by_seq_idx(seq_count, SIZE_MAX);
			std::vector <segment_map::mapped_type *> lhs_segments_by_segment_number;
			std::vector <segment_map::mapped_type *> rhs_segments_by_segment_number;
			std::set <std::pair <std::size_t, std::size_t>> seen_edges;
			
			fg::length_type lb{};
			if (block_count)
			{
				// First block.
				lb::log_time(std::cerr) << "Block 1/" << block_count << "…\n";
				
				fg::length_type rb{};
				archive(rb);
				
				handle_block_range(
					reader,
					lb,
					rb,
					handler,
					lhs_segments,
					segment_buffer,
					lhs_segment_numbers_by_seq_idx,
					lhs_segments_by_segment_number
				);
					
				lb = rb;
				
				// Rest of the blocks.
				for (fg::length_type i(1); i < block_count; ++i)
				{
					lb::log_time(std::cerr) << "Block " << (1 + i) << '/' << block_count << "…\n";
					
					// Read the next right bound.
					archive(rb);
					
					handle_block_range(
						reader,
						lb,
						rb,
						handler,
						rhs_segments,
						segment_buffer,
						rhs_segment_numbers_by_seq_idx,
						rhs_segments_by_segment_number
					);
					
					// Count the distinct edges.
					seen_edges.clear();
					for (auto const &[lhs, rhs] : rsv::zip(lhs_segment_numbers_by_seq_idx, rhs_segment_numbers_by_seq_idx))
						seen_edges.emplace(lhs, rhs);
					
					for (auto const &[lhs, rhs] : seen_edges)
					{
						libbio_assert_neq(lhs, SIZE_MAX);
						libbio_assert_neq(rhs, SIZE_MAX);
						auto &lhs_seg(*lhs_segments_by_segment_number[lhs]);
						auto &rhs_seg(*rhs_segments_by_segment_number[rhs]);
						++lhs_seg.edges; // Out-edges
						++rhs_seg.edges; // In-edges
					}
					
					// Output.
					handler.output(i - 1, lhs_segments);
					
					// Update the pointers.
					lb = rb;
					
					{
						using std::swap;
						swap(lhs_segments, rhs_segments);
						swap(lhs_segment_numbers_by_seq_idx, rhs_segment_numbers_by_seq_idx);
						swap(lhs_segments_by_segment_number, rhs_segments_by_segment_number);
					}
				}
				
				// Output the final block.
				handler.output(block_count - 1, lhs_segments);
			}
		}
		else
		{
			// Output a text that consists of concatenated pairs of segments separated by ‘#’ characters.
			// Maintain two ranges, [lb, mid) and [mid, rb).
			
			segment_map segments;
			// These two are not used here but handle_block_range() currently fills them anyway.
			std::vector <std::size_t> segment_numbers_by_seq_idx(reader.handle_count(), SIZE_MAX);
			std::vector <segment_map::mapped_type *> segments_by_segment_number;
			
			fg::length_type lb{};
			if (block_count)
			{
				fg::length_type mid{};
				archive(mid);
				for (fg::length_type i(1); i < block_count; ++i)
				{
					lb::log_time(std::cerr) << "Block " << i << '/' << (block_count - 1) << "…\n";
					
					// Read the next right bound.
					fg::length_type rb{};
					archive(rb);
					
					handle_block_range(
						reader,
						lb,
						rb,
						handler,
						segments,
						segment_buffer,
						segment_numbers_by_seq_idx,
						segments_by_segment_number
					);
					handler.output(i - 1, segments);
					
					// Update the pointers.
					lb = mid;
					mid = rb;
				}
			}
		}

		handler.finish();
	}
	
	
	void generate_indexable_text(
		fg::msa_reader &reader,
		char const *sequence_list_path,
		char const *segmentation_path,
		bool const block_contents_given,
		bool const tsv_given,
		bool const omit_segments_given,
		bool const omit_trailing_zero_given,
		bool const skip_output_given
	)
	{
		if (block_contents_given)
		{
			if (omit_trailing_zero_given)
				std::cerr << "WARNING: --omit-trailing-zero has no effect when outputting block contents.\n";
			
			if (tsv_given)
			{
				block_content_tsv_output_handler handler(omit_segments_given, skip_output_given);
				generate_indexable_text(reader, sequence_list_path, segmentation_path, handler, block_contents_given);
			}
			else
			{
				block_content_binary_output_handler handler(omit_segments_given, skip_output_given);
				generate_indexable_text(reader, sequence_list_path, segmentation_path, handler, block_contents_given);
			}
		}
		else
		{
			if (tsv_given || omit_segments_given)
				std::cerr << "WARNING: --tsv and --omit-segments do not have an effect when outputting text for BWT indexing.\n";
			
			indexable_text_output_handler handler(omit_trailing_zero_given, skip_output_given);
			generate_indexable_text(reader, sequence_list_path, segmentation_path, handler, block_contents_given);
		}
	}


	std::ostream &synchronize_ostream(std::ostream &stream)
	{
		// FIXME: This should return a std::osyncstream but my libc++ doesn’t yet have it.
		return stream;
	}
	
	
	struct founder_graph_index_construction_delegate : public fg::founder_graph_index_construction_delegate
	{
		void zero_occurrences_for_segment(
			fg::length_type const block_idx,
			fg::length_type const seg_idx,
			std::string const &segment,
			char const cc,
			fg::length_type const pos
		) override
		{
			synchronize_ostream(std::cerr) << "ERROR: got zero occurrences when searching for ‘" << cc << "’ at index " << pos << " of segment " << seg_idx << " (block " << block_idx << "): “" << segment << "”.\n";
		}
		
		void unexpected_number_of_occurrences_for_segment(
			fg::length_type const block_idx,
			fg::length_type const seg_idx,
			std::string const &segment,
			fg::length_type const expected_count,
			fg::length_type const actual_count
		) override
		{
			synchronize_ostream(std::cerr) << "ERROR: got " << actual_count << " occurrences while " << expected_count << " were expected when searching for segment " << seg_idx << " (block " << block_idx << "): “" << segment << "”.\n";
		}
		
		void position_in_b_already_set(fg::length_type const pos) override
		{
			synchronize_ostream(std::cerr) << "ERROR: position " << pos << " in B already set.\n";
		}
		
		void position_in_e_already_set(fg::length_type const pos) override
		{
			synchronize_ostream(std::cerr) << "ERROR: position " << pos << " in E already set.\n";
		}
	};
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
	std::cin.tie(nullptr);					// We don't require any input from the user.
	
	std::cerr << "Invocation:\n";
	for (int i(0); i < argc; ++i)
	{
		if (i)
			std::cerr << ' ';
		std::cerr << argv[i];
	}
	std::cerr << '\n';
	
	if (args_info.generate_indexable_texts_given)
	{
		if (args_info.gzip_input_given)
		{
			fg::bgzip_msa_reader reader;
			generate_indexable_text(
				reader,
				args_info.sequence_list_arg,
				args_info.segmentation_arg,
				args_info.block_contents_given,
				args_info.tsv_given,
				args_info.omit_segments_given,
				args_info.omit_trailing_zero_given,
				args_info.skip_output_given
			);
		}
		else
		{
			fg::text_msa_reader reader;
			generate_indexable_text(
				reader,
				args_info.sequence_list_arg,
				args_info.segmentation_arg,
				args_info.block_contents_given,
				args_info.tsv_given,
				args_info.omit_segments_given,
				args_info.omit_trailing_zero_given,
				args_info.skip_output_given
			);
		}
	}
	else if (args_info.generate_index_given)
	{
		fg::founder_graph_index founder_index;
		founder_graph_index_construction_delegate delegate;
		if (founder_index.construct(args_info.text_path_arg, args_info.sa_path_arg, args_info.bwt_path_arg, args_info.block_contents_path_arg, true, delegate))
		{
			cereal::PortableBinaryOutputArchive archive{std::cout};
			archive(founder_index);
		}
	}
	else if (args_info.read_block_contents_given)
	{
		read_block_contents(args_info.read_block_contents_arg, !args_info.without_segments_given);
	}
	else
	{
		std::cerr << "Unknown mode given.\n";
		return EXIT_FAILURE;
	}
	
	return EXIT_SUCCESS;
}
