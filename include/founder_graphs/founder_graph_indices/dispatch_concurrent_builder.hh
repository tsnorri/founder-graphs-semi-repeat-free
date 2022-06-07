/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_FOUNDER_GRAPH_INDICES_DISPATCH_CONCURRENT_BUILDER_HH
#define FOUNDER_GRAPHS_FOUNDER_GRAPH_INDICES_DISPATCH_CONCURRENT_BUILDER_HH

#include <founder_graphs/founder_graph_indices/block_graph.hh>
#include <founder_graphs/founder_graph_indices/path_index.hh>
#include <libbio/dispatch.hh>


namespace founder_graphs::founder_graph_indices {
	
	namespace dispatch_concurrent_builder_support {
		
		template <typename, typename>
		class concurrent_builder; // Fwd.
	}
	
	
	struct dispatch_concurrent_builder_delegate
	{
		virtual ~dispatch_concurrent_builder_delegate() {}
		
		virtual void reading_bit_vector_values() {}
		virtual void processing_bit_vector_values() {}
		virtual void filling_integer_vectors() {}
	};
	
	
	class dispatch_concurrent_builder
	{
		template <typename, typename>
		friend class dispatch_concurrent_builder_support::concurrent_builder;
		
	protected:
		libbio::dispatch_ptr <dispatch_queue_t>		m_concurrent_queue;
		libbio::dispatch_ptr <dispatch_queue_t>		m_serial_queue;
		libbio::dispatch_ptr <dispatch_group_t>		m_group;
		libbio::dispatch_ptr <dispatch_semaphore_t>	m_sema;				// Limit the number of blocks read in the current thread.
		std::size_t									m_chunk_size{4};
		std::size_t									m_buffer_count{16};
		
	public:
		dispatch_concurrent_builder() = default;
		
		dispatch_concurrent_builder(
			libbio::dispatch_ptr <dispatch_queue_t> concurrent_queue,
			libbio::dispatch_ptr <dispatch_queue_t> serial_queue,
			std::size_t chunk_size = 4,
			std::size_t buffer_count = 16
		):
			m_concurrent_queue(concurrent_queue),
			m_serial_queue(serial_queue),
			m_group(dispatch_group_create()),
			m_sema(dispatch_semaphore_create(buffer_count)),
			m_chunk_size(chunk_size),
			m_buffer_count(buffer_count)
		{
		}
		
		void build_supporting_data_structures(
			block_graph const &gr,
			csa_type const &csa,
			reverse_csa_type const &reverse_csa,
			path_index_support &support,
			dispatch_concurrent_builder_delegate &delegate
		);
	};
}

#endif
