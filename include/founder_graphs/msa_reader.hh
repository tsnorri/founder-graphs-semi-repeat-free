/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_MSA_READER_HH
#define FOUNDER_GRAPHS_MSA_READER_HH

#include <founder_graphs/bgzip_reader.hh>
#include <functional>
#include <libbio/dispatch/dispatch_ptr.hh>
#include <libbio/file_handle.hh>
#include <ostream>
#include <string>
#include <vector>


namespace founder_graphs {
	
	enum class range_overlap_type
	{
		INCLUDES,
		LEFT_OVERLAP,
		RIGHT_OVERLAP,
		DISJOINT
	};
	
	
	// FIXME: Consider merging with reverse_msa_reader, add random access. reverse_msa_reader handles one character at a time, though, while msa_reader allows to specify a range of characters.
	class msa_reader
	{
	public:
		typedef std::span <char>							span_type;
		typedef std::vector <char>							buffer_type;
		typedef std::vector <buffer_type>					buffer_vector;
		typedef std::vector <span_type>						span_vector;
		typedef std::function <bool(span_vector const &)>	fill_buffer_callback_type;
		
	protected:
		buffer_vector						m_buffers;
		span_vector							m_spans;
		
	public:
		virtual ~msa_reader() {}
		virtual void add_file(std::string const &path) = 0;
		virtual void prepare() = 0;
		// FIXME: the following functions differ from reverse_msa_reader in that the latter does not have the range parameters.
		// The range is half-open.
		virtual bool fill_buffer(std::size_t const lb, std::size_t const rb, fill_buffer_callback_type &) = 0;
		bool fill_buffer(std::size_t const lb, std::size_t const rb, fill_buffer_callback_type &&cb) { return fill_buffer(lb, rb, cb); }
		
		buffer_vector const &buffers() const { return m_buffers; }
		virtual std::size_t aligned_size() const = 0;
		virtual std::size_t handle_count() const = 0;
	};
	
	
	class text_msa_reader final : public msa_reader
	{
	protected:
		std::vector <libbio::file_handle>	m_handles;
		std::size_t							m_preferred_block_size{}; // Hopefully the same for all handles.
		std::size_t							m_aligned_size{};
		std::size_t							m_file_position{};
	
	public:
		void add_file(std::string const &path) override;
		void prepare() override;
		using msa_reader::fill_buffer;
		bool fill_buffer(std::size_t const lb, std::size_t const rb, fill_buffer_callback_type &) override;
		
		std::size_t aligned_size() const override { return m_aligned_size; }
		std::size_t handle_count() const override { return m_handles.size(); }
	};
	
	
	class bgzip_msa_reader final : public msa_reader
	{
	protected:
		struct block_range
		{
			std::size_t		block_lb{};
			std::size_t		block_rb{};
		};

		friend std::ostream &operator<<(std::ostream &, block_range const &);
		
	protected:
		std::vector <bgzip_reader>				m_handles;
		std::vector <block_range>				m_current_block_ranges;
		libbio::dispatch_ptr <dispatch_group_t>	m_decompress_group;
		
	public:
		void add_file(std::string const &path) override;
		void prepare() override;
		using msa_reader::fill_buffer;
		bool fill_buffer(std::size_t const lb, std::size_t const rb, fill_buffer_callback_type &) override;
		
		std::size_t aligned_size() const override { return (m_handles.empty() ? 0 : m_handles.front().uncompressed_size()); }
		std::size_t handle_count() const override { return m_handles.size(); }
		
	private:
		template <range_overlap_type t_overlap_type>
		void update_decompressed(
			std::size_t const handle_idx,
			std::size_t const lb,
			std::size_t const rb,
			std::size_t const block_lb,
			std::size_t const block_rb
		);
	};


	inline std::ostream &operator<<(std::ostream &os, bgzip_msa_reader::block_range const &br)
	{
		os << "block_lb: " << br.block_lb << " block_rb: " << br.block_rb;
		return os;
	}
}

#endif
