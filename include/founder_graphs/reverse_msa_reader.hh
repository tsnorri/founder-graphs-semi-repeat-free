/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_REVERSE_MSA_READER_HH
#define FOUNDER_GRAPHS_REVERSE_MSA_READER_HH

#include <libbio/file_handle.hh>
#include <string>
#include <vector>


namespace founder_graphs {
	
	class reverse_msa_reader
	{
	public:
		typedef std::vector <char>		buffer_type;
	
	protected:
		std::vector <libbio::file_handle>	m_handles;
		buffer_type							m_buffer;
		std::size_t							m_file_position{};
		std::size_t							m_aln_size{};
		std::size_t							m_preferred_block_size{}; // Hopefully the same for all handles.
	
	public:
		void add_file(std::string const &path);
		void prepare();
		bool fill_buffer();
		std::size_t aligned_size() const { return m_aln_size; }
		std::size_t block_size() const { return m_preferred_block_size; }
		std::size_t handle_count() const { return m_handles.size(); }
		buffer_type const &buffer() const { return m_buffer; }
	};
}

#endif
