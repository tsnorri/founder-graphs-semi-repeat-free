/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_REVERSE_MSA_READER_HH
#define FOUNDER_GRAPHS_REVERSE_MSA_READER_HH

#include <founder_graphs/bgzip_reader.hh>
#include <functional>
#include <libbio/dispatch/dispatch_ptr.hh>
#include <libbio/file_handle.hh>
#include <string>
#include <vector>


namespace founder_graphs {
	
	// TODO: Make this set of classes more general-purpose by e.g. by allowing traversal from the beginning or random access.
	class reverse_msa_reader
	{
	public:
		typedef std::vector <char>			buffer_type;
		typedef std::function <bool(bool)>	fill_buffer_callback_type;
		
	protected:
		buffer_type							m_buffer;
		std::size_t							m_file_position{};
		std::size_t							m_current_block_size{};
		
	public:
		virtual void add_file(std::string const &path) = 0;
		virtual void prepare() = 0;
		virtual bool fill_buffer(fill_buffer_callback_type &) = 0;
		bool fill_buffer(fill_buffer_callback_type &&cb) { return fill_buffer(cb); }
		
		buffer_type const &buffer() const { return m_buffer; }
		std::size_t block_size() const { return m_current_block_size; }
		virtual std::size_t aligned_size() const = 0;
		virtual std::size_t handle_count() const = 0;
	};
	
	
	class text_reverse_msa_reader final : public reverse_msa_reader
	{
	protected:
		std::vector <libbio::file_handle>	m_handles;
		std::size_t							m_preferred_block_size{}; // Hopefully the same for all handles.
		std::size_t							m_aligned_size{};
		
	public:
		void add_file(std::string const &path) override;
		void prepare() override;
		using reverse_msa_reader::fill_buffer;
		bool fill_buffer(fill_buffer_callback_type &) override;
		
		std::size_t aligned_size() const override { return m_aligned_size; }
		std::size_t handle_count() const override { return m_handles.size(); }
	};
	
	
	class bgzip_reverse_msa_reader final : public reverse_msa_reader
	{
	protected:
		std::vector <bgzip_reader>				m_handles;
		libbio::dispatch_ptr <dispatch_group_t>	m_decompress_group;
		
	public:
		void add_file(std::string const &path) override;
		void prepare() override;
		using reverse_msa_reader::fill_buffer;
		bool fill_buffer(fill_buffer_callback_type &) override;
		
		std::size_t aligned_size() const override { return (m_handles.empty() ? 0 : m_handles.front().uncompressed_size()); }
		std::size_t handle_count() const override { return m_handles.size(); }
	};
}

#endif
