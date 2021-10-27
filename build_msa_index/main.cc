/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */


#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <cereal/archives/portable_binary.hpp>
#include <founder_graphs/msa_index.hh>
#include <iostream>
#include <libbio/assert.hh>
#include <libbio/file_handle.hh>
#include <libbio/file_handling.hh>
#include "cmdline.h"

namespace fg	= founder_graphs;
namespace lb	= libbio;
namespace ios	= boost::iostreams;


namespace {
	
	std::size_t check_file_size(lb::file_handle const &handle)
	{
		auto const fd(handle.get());
		struct stat sb{};
		
		if (-1 == fstat(fd, &sb))
			throw std::runtime_error(strerror(errno));
		
		if (sb.st_size < 0)
			return 0;
		
		return sb.st_size;
	}
	
	
	void open_stream(lb::file_handle const &handle, lb::file_istream &stream)
	{
		ios::file_descriptor_source source(handle.get(), ios::never_close_handle);
		stream.open(source);
		stream.exceptions(std::istream::badbit);
	}
	
	
	std::size_t handle_file(std::string const &path, std::size_t const size, bool const input_is_gzipped, sdsl::bit_vector &buffer, cereal::PortableBinaryOutputArchive &archive)
	{
		std::cerr << "Handling " << path << "…" << std::flush;
		lb::file_handle handle(lb::open_file_for_reading(path));
		
		// Clear the gap buffer.
		if (input_is_gzipped)
		{
			if (SIZE_MAX == size)
				buffer.resize(0);
			else
				buffer.assign(size, 0);
		}
		else
		{
			// Check the file size.
			auto const actual_size(check_file_size(handle));
			if (SIZE_MAX != size)
				libbio_always_assert_eq(size, actual_size);
			
			// Clear the buffer.
			libbio_assert_lt(0, actual_size);
			buffer.assign(actual_size, 0);
			libbio_assert_eq(buffer.size(), actual_size);
		}
		
		// Handle the sequence.
		lb::file_istream stream;
		open_stream(handle, stream);
		
		ios::filtering_istream in;
		if (input_is_gzipped)
			in.push(ios::gzip_decompressor());
		in.push(stream);
		
		char ch{};
		std::size_t i(0);
		std::size_t gap_count(0);
		while (in >> std::noskipws >> ch)
		{
			if ('-' == ch)
			{
				if (input_is_gzipped && SIZE_MAX == size)
					buffer.resize(1 + i, 0);
				buffer[i] = 1;
				++gap_count;
			}
			
			++i;
		}
		
		if (input_is_gzipped && SIZE_MAX == size)
			buffer.resize(i, 0);
		
		libbio_always_assert(i == buffer.size());
		
		// Create a compressed index and prepare rank and select support.
		fg::aligned_sequence_index seq_idx(buffer);
		seq_idx.prepare_rank_and_select_support();
		
		std::cerr << " handled " << i << " characters; found " << gap_count << " gap characters.\n";
		
		// Archive.
		archive(seq_idx);
		
		return i;
	}
	
	
	void build_msa_index(char const *sequence_list_path, bool const input_is_gzipped)
	{
		lb::file_istream stream;
		lb::open_file_for_reading(sequence_list_path, stream);
		
		std::vector <std::string> paths;
		std::string line;
		std::size_t file_size{SIZE_MAX};
		while (std::getline(stream, line))
			paths.push_back(line);

		// Prepare the output archive.
		cereal::PortableBinaryOutputArchive archive(std::cout);
		{
			std::size_t const size(paths.size());
			archive(cereal::make_size_tag(size));
		}
		
		// Handle the inputs.
		sdsl::bit_vector buffer;
		for (auto const &path : paths)
			file_size = handle_file(path, file_size, input_is_gzipped, buffer, archive);
		
		std::cout << std::flush;
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
	
	build_msa_index(args_info.sequence_list_arg, args_info.gzip_input_flag);
	
	return EXIT_SUCCESS;
}
