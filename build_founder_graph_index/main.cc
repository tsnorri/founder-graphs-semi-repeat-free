/*
 * Copyright (c) 2021-2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/string.hpp>
#include <filesystem>
#include <founder_graphs/founder_graph_indices/dispatch_concurrent_builder.hh>
#include <founder_graphs/founder_graph_indices/path_index.hh>
#include <libbio/file_handle.hh>
#include <libbio/file_handling.hh>
#include <optional>
#include <sdsl/construct.hpp>
#include <string>
#include "cmdline.h"

namespace fg	= founder_graphs;
namespace fgi	= founder_graphs::founder_graph_indices;
namespace fs	= std::filesystem;
namespace lb	= libbio;
namespace ios	= boost::iostreams;
namespace rsv	= ranges::views;


namespace {
	
	constexpr bool logical_xor(bool const aa, bool const bb)
	{
		return !((aa && bb) || (!(aa || bb)));
	}
	
	
	std::optional <std::string> make_optional(char const *str)
	{
		if (str)
			return {str};
		
		return {std::nullopt};
	}
	
	
	// Postcondition: path_template contains the path of the temporary file.
	template <typename t_stream>
	void open_temporary_file_for_rw(std::string &path_template, std::size_t const suffix_length, t_stream &stream)
	{
		// Open a temporary file.
		lb::file_handle temp_handle(lb::open_temporary_file_for_rw(path_template, suffix_length));
	
		// Make the stream not own the handle.
		stream.open(temp_handle.get(), ios::close_handle);
		temp_handle.release();
		stream.exceptions(std::istream::badbit);
	}
	
	
	template <typename t_lhs_stream, typename t_rhs_stream>
	void reverse_indexable_text(t_lhs_stream &forward_stream, t_rhs_stream &reverse_stream)
	{
		auto const fd(forward_stream->handle());
		
		// Determine the file size and the optimal block size.
		// It might be a good idea to do the same for reverse_stream but that
		// would complicate things. Besides, the streams have their own buffers.
		struct stat sb{};
		if (-1 == fstat(fd, &sb))
			throw std::runtime_error(strerror(errno));
		
		std::size_t write_pos(sb.st_size);
		std::vector <char> buffer(sb.st_blksize, 0);
		
		ios::seek(forward_stream, 0, std::ios_base::beg);
		libbio_assert_eq(0, errno);

		do
		{
			// Read and move the writing stream to the corresponding position.
			auto const read_count(ios::read(forward_stream, buffer.data(), buffer.size()));
			libbio_always_assert_lte(read_count, write_pos);
			libbio_assert_eq(0, errno);
			write_pos -= read_count;
			ios::seek(reverse_stream, write_pos, std::ios_base::beg);
			libbio_assert_eq(0, errno);

			// Reverse the buffer contents and write.
			std::reverse(buffer.begin(), buffer.begin() + read_count);
			ios::write(reverse_stream, buffer.data(), read_count);
			libbio_assert_eq(0, errno);
		} while (write_pos);
		
		reverse_stream << std::flush;
	}
	
	
	void build_csas_and_wait(
		std::string const &text_path,
		std::string const &reverse_text_path,
		dispatch_group_t group,
		dispatch_queue_t queue,
		fgi::path_index &index
	)
	{
		// This would likely be more efficient if it were somehow possible to co-ordinate the I/O operations
		// when building the CSAs.
		dispatch_group_async(group, queue, ^{
			fgi::csa_type csa;
			sdsl::construct(csa, text_path, 1);
			index.set_csa(std::move(csa));
		});
		
		dispatch_group_async(group, queue, ^{
			fgi::reverse_csa_type csa;
			sdsl::construct(csa, reverse_text_path, 1);
			index.set_reverse_csa(std::move(csa));
		});
		
		dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
	}
	
	
	struct indexable_sequence_output_delegate final : public fgi::indexable_sequence_output_delegate
	{
		std::ostream &os; // Not owned.
		
		indexable_sequence_output_delegate(std::ostream &os_):
			os(os_)
		{
		}
		
		virtual void output_segment(
			std::size_t const block_idx,
			std::size_t const file_offset,
			std::size_t const seg_idx,
			std::size_t const seg_size
		) override
		{
			os << "Segment\t" << block_idx << '\t' << file_offset << '\t' << seg_idx << '\t' << seg_size << '\n';
		}
		
		virtual void output_edge(
			std::size_t const block_idx,
			std::size_t const file_offset,
			std::size_t const lhs_seg_idx,
			std::size_t const rhs_seg_idx,
			std::size_t const lhs_seg_size,
			std::size_t const rhs_seg_size
		) override
		{
			os << "Edge\t" << block_idx << '\t' << file_offset << '\t' << lhs_seg_idx << '\t' << rhs_seg_idx << '\t' << lhs_seg_size << '\t' << rhs_seg_size << '\n';
		}
		
		virtual void finish() override { os << std::flush; }
	};
	
	
	struct dispatch_concurrent_builder_delegate : public fgi::dispatch_concurrent_builder_delegate
	{
		virtual void reading_bit_vector_values() override
		{
			lb::log_time(std::cerr) << " Reading bit vector values…\n";
		}
		
		virtual void processing_bit_vector_values() override
		{
			lb::log_time(std::cerr) << " Processing bit vector values…\n";
		}
		
		virtual void filling_integer_vectors() override
		{
			lb::log_time(std::cerr) << " Filling integer vectors…\n";
		}
	};
	
	
	class index_builder
	{
	protected:
		lb::dispatch_ptr <dispatch_queue_t>	m_serial_queue;
		std::string							m_sequence_list_path;
		std::string							m_segmentation_path;
		std::optional <std::string>			m_indexable_text_input_path;
		std::optional <std::string>			m_reverse_indexable_text_input_path;
		std::optional <std::string>			m_indexable_text_output_path;
		std::optional <std::string>			m_indexable_text_stats_output_path;
		std::optional <std::string>			m_reverse_indexable_text_output_path;
		std::optional <std::string>			m_graphviz_output_path;
		std::optional <std::string>			m_index_input_path;
		std::uint16_t						m_buffer_count{};
		std::uint16_t						m_chunk_size{};
		bool								m_input_is_bgzipped{};
		bool								m_should_skip_csa{};
		bool								m_should_skip_support{};
		bool								m_should_skip_output{};
	
	public:
		index_builder() = default;
		
		index_builder(gengetopt_args_info const &args_info):
			m_serial_queue(dispatch_queue_create("fi.iki.tsnorri.founder-graphs-semi-repeat-free.serial-queue", DISPATCH_QUEUE_SERIAL)),
			m_sequence_list_path(args_info.sequence_list_arg),
			m_segmentation_path(args_info.segmentation_arg),
			m_indexable_text_input_path(make_optional(args_info.indexable_text_input_arg)),
			m_reverse_indexable_text_input_path(make_optional(args_info.reverse_indexable_text_input_arg)),
			m_indexable_text_output_path(make_optional(args_info.indexable_text_output_arg)),
			m_indexable_text_stats_output_path(make_optional(args_info.indexable_text_stats_output_arg)),
			m_reverse_indexable_text_output_path(make_optional(args_info.reverse_indexable_text_output_arg)),
			m_graphviz_output_path(make_optional(args_info.graphviz_output_arg)),
			m_index_input_path(make_optional(args_info.index_input_arg)),
			m_buffer_count(args_info.buffer_count_arg),
			m_chunk_size(args_info.chunk_size_arg),
			m_input_is_bgzipped(args_info.bgzip_input_given),
			m_should_skip_csa(args_info.skip_csa_given),
			m_should_skip_support(args_info.skip_support_given),
			m_should_skip_output(args_info.skip_output_given)
		{
		}
		
		void process();
		void operator()() { process(); } // For lb::dispatch().
	};
	
	
	void index_builder::process()
	{
		fgi::path_index index;
		lb::dispatch_ptr <dispatch_queue_t> concurrent_queue(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), true);
		lb::dispatch_ptr <dispatch_group_t> group(dispatch_group_create());
		
		// Load if requested.
		if (m_index_input_path)
		{
			lb::log_time(std::cerr) << "Loading the index…\n";
			lb::file_istream stream;
			lb::open_file_for_reading(*m_index_input_path, stream);
			cereal::PortableBinaryInputArchive iarchive(stream);
			iarchive(index);
		}
		
		// Build an uncompressed founder graph.
		lb::log_time(std::cerr) << "Loading the segmentation…\n";
		fgi::block_graph graph;
		fgi::read_optimized_segmentation(
			m_sequence_list_path.c_str(),
			m_segmentation_path.c_str(),
			m_input_is_bgzipped,
			graph
		);
			
		if (m_graphviz_output_path)
		{
			lb::log_time(std::cerr) << "Outputting the uncompressed founder graph as a Graphviz file…\n";
			lb::file_ostream stream;
			lb::open_file_for_writing(*m_graphviz_output_path, stream, lb::writing_open_mode::CREATE);
			fgi::write_graphviz(graph, stream);
		}
		
		// Check if the indexable text should be built.
		if (!m_should_skip_csa)
		{
			if (m_indexable_text_input_path && m_reverse_indexable_text_input_path)
			{
				lb::log_time(std::cerr) << "Building the CSA using the given input…\n";
				build_csas_and_wait(*m_indexable_text_input_path, *m_reverse_indexable_text_input_path, *group, *concurrent_queue, index);
			}
			else
			{
				lb::log_time(std::cerr) << "Generating the indexable text…\n";
				lb::file_iostream forward_stream;
				lb::file_ostream reverse_stream;
			
				// Build the indexable and reverse texts.
				if (m_indexable_text_output_path)
					lb::open_file_for_rw(*m_indexable_text_output_path, forward_stream, lb::writing_open_mode::CREATE);
				else
				{
					m_indexable_text_output_path = "indexable-text.XXXXXX.txt";
					open_temporary_file_for_rw(*m_indexable_text_output_path, 4, forward_stream); // ".txt"
				}
			
				if (m_reverse_indexable_text_output_path)
					lb::open_file_for_writing(*m_reverse_indexable_text_output_path, reverse_stream, lb::writing_open_mode::CREATE);
				else
				{
					m_reverse_indexable_text_output_path = "reverse-indexable-text.XXXXXX.txt";
					open_temporary_file_for_rw(*m_reverse_indexable_text_output_path, 4, reverse_stream); // ".txt"
				}
			
				lb::log_time(std::cerr) << "Writing to " << (*m_indexable_text_output_path) << " and to " << (*m_reverse_indexable_text_output_path) << "…\n";
				
				if (m_indexable_text_stats_output_path)
				{
					lb::file_ostream stats_stream;
					lb::log_time(std::cerr) << "Writing segment offsets to " << (*m_indexable_text_stats_output_path) << "…\n";
					lb::open_file_for_writing(*m_indexable_text_stats_output_path, stats_stream, lb::writing_open_mode::CREATE);
					indexable_sequence_output_delegate delegate(stats_stream);
					
					fgi::write_indexable_sequence(graph, forward_stream, delegate);
				}
				else
				{
					fgi::write_indexable_sequence(graph, forward_stream);
				}
				
				reverse_indexable_text(forward_stream, reverse_stream);
				
				// Build the indices.
				lb::log_time(std::cerr) << "Building the CSAs…\n";
				build_csas_and_wait(*m_indexable_text_output_path, *m_reverse_indexable_text_output_path, *group, *concurrent_queue, index);
			}
		}
		
		if (!m_should_skip_support)
		{
			// Check that we have a CSA.
			auto const csa_size(index.get_csa().size());
			auto const reverse_csa_size(index.get_reverse_csa().size());
			if (!csa_size)
			{
				lb::log_time(std::cerr) << "ERROR: The forward CSA is empty.\n";
				std::exit(EXIT_FAILURE);
			}
			
			if (csa_size != reverse_csa_size)
			{
				lb::log_time(std::cerr) << "ERROR: The forward and reverse CSAs have different sizes (" << csa_size << " and " << reverse_csa_size << ").\n";
				std::exit(EXIT_FAILURE);
			}
			
			// Build the supporting data structures.
			lb::log_time(std::cerr) << "Building the supporting data structures…\n";
			fgi::path_index_support support;
			fgi::dispatch_concurrent_builder builder(concurrent_queue, m_serial_queue, m_buffer_count);
			dispatch_concurrent_builder_delegate delegate;
			builder.build_supporting_data_structures(graph, index.get_csa(), index.get_reverse_csa(), support, delegate);
			index.set_support(std::move(support));
		}
		
		// Output if needed.
		if (!m_should_skip_output && (!(m_should_skip_csa && m_should_skip_support)))
		{
			lb::log_time(std::cerr) << "Writing the index to stdout…\n";
			cereal::PortableBinaryOutputArchive oarchive(std::cout);
			oarchive(index);
			std::cout << std::flush;
		}
		
		std::exit(EXIT_SUCCESS);
	}
	
	
	void output_space_breakdown(char const *index_path, bool const should_output_json)
	{
		fgi::path_index index;
		
		{
			lb::file_istream stream;
			lb::open_file_for_reading(index_path, stream);
			cereal::PortableBinaryInputArchive iarchive(stream);
			iarchive(index);
		}
		
		if (should_output_json)
			sdsl::write_structure <sdsl::JSON_FORMAT>(index, std::cout);
		else
			sdsl::write_structure <sdsl::HTML_FORMAT>(index, std::cout);
		std::cout << std::flush;
	}
	
	
	static index_builder s_index_builder;
	
	
	// Try to make sure that this function does not get inlined since the
	// try-catch block can somehow problems with dispatch_main()
	// (“terminate called without an active exception”).
	void __attribute__((noinline)) do_process(gengetopt_args_info const &args_info) noexcept
	{
		try
		{
			// Check if space breakdown is to be output.
			if (args_info.space_breakdown_given)
			{
				if (!args_info.index_input_arg)
				{
					std::cerr << "ERROR: --space-breakdown was given but --index-input was not.\n";
					std::exit(EXIT_FAILURE);
				}
				
				output_space_breakdown(args_info.index_input_arg, false);
				std::exit(EXIT_SUCCESS);
			}
			
			// Otherwise build the index.
			if (logical_xor(args_info.indexable_text_input_arg, args_info.reverse_indexable_text_input_arg))
			{
				std::cerr << "ERROR: Either none or both of --indexable-text-input and --reverse-indexable-text-input must be given.\n";
				std::exit(EXIT_FAILURE);
			}
			
			if (args_info.buffer_count_arg <= 0)
			{
				std::cerr << "ERROR: Buffer count must be positive.\n";
				std::exit(EXIT_FAILURE);
			}
			
			if (args_info.chunk_size_arg <= 0)
			{
				std::cerr << "ERROR: Chunk size must be positive.\n";
				std::exit(EXIT_FAILURE);
			}
			
			s_index_builder = index_builder(args_info);
			
			lb::dispatch(s_index_builder).async <>(dispatch_get_main_queue());
		}
		catch (std::exception const &exc)
		{
			std::cerr << "Top-level exception handler caught an exception: " << exc.what() << ".\n";
			std::exit(EXIT_FAILURE);
		}
		catch (...)
		{
			std::cerr << "Top-level exception handler caught a non-std::exception.\n";
			std::exit(EXIT_FAILURE);
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
	std::cin.tie(nullptr);					// We don't require any input from the user.
	
	std::cerr << "Invocation:\n";
	for (int i(0); i < argc; ++i)
	{
		if (i)
			std::cerr << ' ';
		std::cerr << argv[i];
	}
	std::cerr << '\n';
	
	do_process(args_info);
	
	dispatch_main();
	// Not reached.
	return EXIT_SUCCESS;
}
