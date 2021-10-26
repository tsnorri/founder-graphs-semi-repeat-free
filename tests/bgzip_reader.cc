/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <algorithm>
#include <catch2/catch.hpp>
#include <founder_graphs/bgzip_reader.hh>
#include <libbio/file_handling.hh>

namespace fg	= founder_graphs;
namespace gen	= Catch::Generators;
namespace lb	= libbio;


namespace {

	std::size_t compare_blocks(fg::bgzip_reader &bgzip_reader, lb::file_istream &expected_stream, std::vector <char> &buffer_1, std::vector <char> &buffer_2)
	{
		bgzip_reader.read_current_block();
		
		// Decompress.
		auto const offset(bgzip_reader.current_block_uncompressed_offset());
		auto const size(bgzip_reader.current_block_uncompressed_size());
		buffer_1.resize(size);
		bgzip_reader.decompress(std::span(buffer_1.data(), buffer_1.data() + size));
		
		// Read the expected data.
		buffer_2.resize(size);
		expected_stream.seekg(offset);
		expected_stream.read(buffer_2.data(), size);
		REQUIRE(std::equal(buffer_1.begin(), buffer_1.end(), buffer_2.begin(), buffer_2.end()));
		
		return size;
	}
}


SCENARIO("bgzip_reader can read a file")
{
	GIVEN("a bgzip file")
	{
		// Compressed file
		fg::bgzip_reader bgzip_reader;
		bgzip_reader.open("test-files/random-200000B.txt.gz");
		
		// Expected contents
		lb::file_istream expected_stream;
		lb::open_file_for_reading("test-files/random-200000B.txt", expected_stream);
		
		WHEN("the file is read")
		{
			THEN("its contents match the original")
			{
				std::vector <char> buffer_1, buffer_2;
				auto const block_count(bgzip_reader.block_count());
				size_t total_uncompressed_size{};
				for (std::size_t i(0); i < block_count; ++i)
				{
					total_uncompressed_size += compare_blocks(bgzip_reader, expected_stream, buffer_1, buffer_2);
					bgzip_reader.block_seek_next();
				}
				REQUIRE(200000 == total_uncompressed_size);
			}
		}
	}
	
	
	GIVEN("A bgzip file")
	{
		// Compressed file
		fg::bgzip_reader bgzip_reader;
		bgzip_reader.open("test-files/random-200000B.txt.gz");
		
		// Expected contnets
		lb::file_istream expected_stream;
		lb::open_file_for_reading("test-files/random-200000B.txt", expected_stream);
		
		WHEN("the file is read from the last block to the first")
		{
			THEN("its contents match the original")
			{
				std::vector <char> buffer_1, buffer_2;
				auto const block_count(bgzip_reader.block_count());
				std::size_t total_uncompressed_size{};
				for (std::size_t i(0); i < block_count; ++i)
				{
					bgzip_reader.block_seek(block_count - i - 1);
					total_uncompressed_size += compare_blocks(bgzip_reader, expected_stream, buffer_1, buffer_2);
				}
				REQUIRE(200000 == total_uncompressed_size);
			}
		}
	}
}
