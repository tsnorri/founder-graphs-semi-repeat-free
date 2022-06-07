/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#include <founder_graphs/founder_graph_indices/dispatch_concurrent_builder.hh>
#include <founder_graphs/founder_graph_indices/index_construction.hh>
#include <founder_graphs/sort.hh>
#include <founder_graphs/utility.hh>
#include <libbio/assert.hh>
#include <libbio/bits.hh>
#include <libbio/function_output_iterator.hh>
#include <range/v3/algorithm/copy.hpp>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/view/drop_last.hpp>
#include <range/v3/view/zip.hpp>


namespace fg	= founder_graphs;
namespace fgi	= founder_graphs::founder_graph_indices;
namespace lb	= libbio;
namespace rsv	= ranges::views;


namespace {
	
	template <typename t_type>
	inline constexpr bool is_const_reference_v = std::is_reference_v <t_type> && std::is_const_v <std::remove_reference_t <t_type>>;
	
	template <typename t_type>
	inline constexpr bool is_nonconst_reference_v = std::is_reference_v <t_type> && (!std::is_const_v <std::remove_reference_t <t_type>>);
	
	
	void assign_bv_contents(sdsl::bit_vector const &src, sdsl::bit_vector &dst, std::size_t const dst_offset)
	{
		libbio_assert_lte(dst_offset + src.size(), dst.size());
		
		auto const dst_word_offset(dst_offset / 64U);
		auto const dst_bit_offset(dst_offset % 64U);
		auto const src_word_count((src.size() + 63U) >> 6U);
		if (dst_bit_offset)
		{
			// Copy (actually bitwise or) from aligned src to unaligned dst.
			std::uint64_t const * const src_(src.data());
			std::uint64_t *dst_(dst.data());
			dst_ += dst_word_offset;
			for (std::size_t i(0); i < src_word_count; ++i)
			{
				std::uint64_t const word(src_[i]);
				*dst_ |= word << dst_bit_offset;
				++dst_;
				*dst_ |= word >> (64U - dst_bit_offset);
			}
		}
		else
		{
			// Faster path.
			auto const *src_data(src.data());
			auto *dst_data(dst.data() + dst_word_offset);
			for (std::size_t i(0); i < src_word_count; ++i)
				dst_data[i] |= src_data[i];
		}
	}
	
	
	template <typename t_bit_vector, typename t_support_type>
	void prepare_support(t_bit_vector const &bv, t_support_type &support)
	{
		support = t_support_type(&bv);
		support.set_vector(&bv);
	}
}


// We need to reference concurrent_builder in a friend declaration in dispatch_concurrent_builder.
// I’m not sure if it were possible if the namespace were e.g.
// founder_graphs::founder_graph_indices::<anonymous>::dispatch_concurrent_builder_support.
namespace founder_graphs::founder_graph_indices::dispatch_concurrent_builder_support {
	
	// Depends on dispatch_concurrent_builder::m_sema.
	template <typename t_buffer>
	class concurrent_builder_buffer_store
	{
	public:
		typedef t_buffer							buffer_type;
		typedef std::vector <buffer_type>			buffer_vector_type;
		
	protected:
		buffer_vector_type							m_buffers;
		std::uint64_t								m_get_idx{};
		std::uint64_t								m_put_idx{};
		
	public:
		concurrent_builder_buffer_store() = default;
		
		explicit concurrent_builder_buffer_store(std::size_t buffer_count):
			m_buffers(buffer_count)
		{
		}
		
		concurrent_builder_buffer_store(std::size_t buffer_count, buffer_type const &buffer):
			m_buffers(buffer_count, buffer)
		{
		}
		
		void get_buffer(buffer_type &dst)
		{
			// Invoked from one queue only.
			auto const idx_(m_get_idx++);
			libbio_assert_lt(idx_, UINT64_MAX); // Needed for the check in put_buffer and for arbitrary buffer counts.
			auto const idx(idx_ % m_buffers.size());
			dst = std::move(m_buffers[idx]);
		}
		
		void put_buffer(buffer_type &&src)
		{
			// Invoked from one queue only.
			auto const idx_(m_put_idx++);
			auto const idx(idx_ % m_buffers.size());
			m_buffers[idx] = std::move(src);
		}
	};
	
	
	template <typename t_state>
	class concurrent_builder_base
	{
	protected:
		t_state m_state;
		
	public:
		concurrent_builder_base() requires(std::is_default_constructible_v <t_state>) = default;
		
		explicit concurrent_builder_base(t_state &&state):
			m_state(std::forward <t_state>(state))
		{
		}
		
		t_state &state() { return m_state; }
		t_state const &state() const { return m_state; }
	};
	
	template <>
	struct concurrent_builder_base <void>
	{
		concurrent_builder_base() = default;
	};
	
	// Base class for processing a block range using a concurrent dispatch queue.
	// State type is here in order to make subclassing a bit easier. (No need to rewrite the constructors.)
	template <typename t_buffer, typename t_state = void>
	class concurrent_builder : public concurrent_builder_base <t_state>
	{
	public:
		typedef concurrent_builder_base <t_state>				base_type;
		typedef t_buffer										buffer_type;
		typedef concurrent_builder_buffer_store <buffer_type>	buffer_store_type;
		
	protected:
		buffer_store_type			m_buffer_store;
		csa_type const				&m_csa;				// Not owned.
		reverse_csa_type const		&m_reverse_csa;		// Not owned.
		block_graph const			&m_graph;			// Not owned.
		dispatch_concurrent_builder	&m_builder;			// Not owned.
		path_index_support			&m_support;			// Not owned.
	
	public:
		virtual ~concurrent_builder() {}
		
#if 0
		concurrent_builder(
			csa_type const &csa,
			block_graph const &graph,
			dispatch_concurrent_builder &builder,
			path_index_support &support,
			buffer_vector_ref buffer_vector
		) requires(std::is_default_constructible_v <base_type>):
			base_type(),
			m_buffer_vector(buffer_vector),
			m_csa(csa),
			m_graph(graph),
			m_builder(builder),
			m_support(support)
		{
		}
#endif
		
		concurrent_builder(
			csa_type const &csa,
			reverse_csa_type const &reverse_csa,
			block_graph const &graph,
			dispatch_concurrent_builder &builder,
			path_index_support &support
		) requires(
			//!std::is_lvalue_reference_v <buffer_vector_var_type> &&
			std::is_default_constructible_v <base_type>
		):
			base_type(),
			m_buffer_store(builder.m_buffer_count),
			m_csa(csa),
			m_reverse_csa(reverse_csa),
			m_graph(graph),
			m_builder(builder),
			m_support(support)
		{
		}
		
		concurrent_builder(
			csa_type const &csa,
			reverse_csa_type const &reverse_csa,
			block_graph const &graph,
			dispatch_concurrent_builder &builder,
			path_index_support &support,
			buffer_type const &buffer								// Copied in buffer vector initialization.
		) /* requires(
			!std::is_lvalue_reference_v <buffer_vector_var_type>	// We manage the buffer vector.
		)*/:
			base_type(),
			m_buffer_store(builder.m_buffer_count, buffer),
			m_csa(csa),
			m_reverse_csa(reverse_csa),
			m_graph(graph),
			m_builder(builder),
			m_support(support)
		{
		}
		
		// For some reason a check for non-void t_state in requires() is not enough.
		// A variant with buffer_type is currently not required by the subclasses.
		template <typename t_state_>
		concurrent_builder(
			csa_type const &csa,
			reverse_csa_type const &reverse_csa,
			block_graph const &graph,
			dispatch_concurrent_builder &builder,
			path_index_support &support,
			t_state_ &&state
		) requires(
			//!std::is_lvalue_reference_v <buffer_vector_var_type> &&	// We manage the buffer vector.
			std::is_same_v <t_state, t_state_>
		):
			base_type(std::forward <t_state_>(state)),
			m_buffer_store(builder.m_buffer_count),
			m_csa(csa),
			m_reverse_csa(reverse_csa),
			m_graph(graph),
			m_builder(builder),
			m_support(support)
		{
		}
		
		// Make sure copies are not made.
		concurrent_builder(concurrent_builder const &) = delete;
		concurrent_builder &operator=(concurrent_builder const &) = delete;
	
	protected:
		// For processing and post-processing the values.
		// We could use CRTP instead but it should not matter b.c. the subclasses can be marked final.
		virtual void process(std::size_t const pos, std::size_t const length, buffer_type &buffer) = 0;
		virtual void postprocess(std::size_t const pos, std::size_t const length, buffer_type &buffer) = 0;
		
		dispatch_queue_t get_concurrent_queue() const { return *m_builder.m_concurrent_queue; }
		dispatch_group_t get_builder_group() const { return *m_builder.m_group; }
		
	public:
		void handle_range(std::size_t const pos, std::size_t const length);
	};
	
	
	template <typename t_buffer, typename t_state>
	void concurrent_builder <t_buffer, t_state>::handle_range(std::size_t const pos, std::size_t const length)
	{
		// Get a buffer and process.
		buffer_type dst;
		m_buffer_store.get_buffer(dst);
		
		lb::dispatch_group_async_fn(
			*m_builder.m_group,
			get_concurrent_queue(),
			[this, pos, length, dst = std::move(dst)]() mutable { // mutable needed b.c. dst needs to be non-const.
				process(pos, length, dst);
			
				// Process the positions in the serial queue.
				lb::dispatch_group_async_fn(
					*m_builder.m_group,
					*m_builder.m_serial_queue,
					[this, pos, length, dst = std::move(dst)]() mutable {
						postprocess(pos, length, dst);
					
						// Return the vector to the buffer.
						m_buffer_store.put_buffer(std::move(dst));
						dispatch_semaphore_signal(*m_builder.m_sema); // The block captures this.
					}
				);
			}
		);
	}
	
	
	// We store the B positions here in order to be able to sort N and X by them.
	struct position_block
	{
		sdsl::int_vector <0>	b_pos;
		sdsl::int_vector <0>	n;
		sdsl::int_vector <0>	x;
	

		position_block(
			sdsl::int_vector <0> const &b_pos_,
			sdsl::int_vector <0> &n_, // Swaps.
			sdsl::int_vector <0> &x_  // Swaps.
		):
			b_pos(b_pos_), // Copy.
			n(0, 0, n_.width()),
			x(0, 0, x_.width())
		{
			using std::swap;
			swap(n, n_);
			swap(x, x_);
		}
		
		
		position_block(
			std::size_t const capacity,
			std::uint8_t const b_width,
			std::uint8_t const n_width,
			std::uint8_t const x_width
		):
			b_pos(0, 0, b_width),
			n(0, 0, n_width),
			x(0, 0, x_width)
		{
			b_pos.reserve(capacity);
			n.reserve(capacity);
			x.reserve(capacity);
		}
		
		auto zip_view() const { return rsv::zip(b_pos, n, x); }
		
		template <typename t_tuple>
		void operator()(t_tuple const &tup) // For merge and function output iterator.
		{
			fg::push_back(b_pos, std::get <0>(tup));
			fg::push_back(n, std::get <1>(tup));
			fg::push_back(x, std::get <2>(tup));
		}
		
		void sort()
		{
			libbio_assert_eq(b_pos.size(), n.size());
			libbio_assert_eq(b_pos.size(), x.size());
			
			auto view(rsv::zip(b_pos, n, x));
			::fg::sort(view.begin(), view.end());
			libbio_assert(ranges::is_sorted(rsv::zip(b_pos, n, x)));
		}
	};
	
	typedef std::vector <position_block>	position_block_vector_type;
	
	
	struct bedinx_vector_builder_state
	{
		position_block_vector_type				position_blocks;
		sdsl::bit_vector						b;
		sdsl::bit_vector						e;
		sdsl::bit_vector						d;
		sdsl::bit_vector						i;
		sdsl::bit_vector						x;
		sdsl::bit_vector						bh;
		sdsl::bit_vector						m;
		sdsl::bit_vector						u;
		
		explicit bedinx_vector_builder_state(
			std::size_t const csa_size,
			std::size_t const x_size,
			std::size_t const bh_size,
			std::size_t const m_size,
			std::size_t const u_size
		):
			b(csa_size, 0),
			e(csa_size, 0),
			d(csa_size, 0),
			i(csa_size, 0),
			x(x_size,   1),
			bh(bh_size, 1),
			m(m_size,   0),
			u(u_size,   0)
		{
		}
	};
	
	
	// For making sure that we get references to the bit vectors and the tuple
	// (of references to BV support data structures) as a value.
	template <typename t_dst_bit_vector, typename t_tuple>
	struct bit_vector_builder
	{
		sdsl::bit_vector	&src;		// Not owned.
		t_dst_bit_vector	&dst;		// Not owned.
		t_tuple				support;	// Tuple of references, owned.
		
		constexpr static inline bool const DESTINATION_IS_UNCOMPRESSED{std::is_same_v <sdsl::bit_vector, t_dst_bit_vector>};
		constexpr static inline bool const USES_FAST_PATH{DESTINATION_IS_UNCOMPRESSED && 0 == std::tuple_size_v <t_tuple>};
		
		bit_vector_builder(sdsl::bit_vector &src_, t_dst_bit_vector &dst_, t_tuple &&support_):
			src(src_),
			dst(dst_),
			support(std::move(support_))
		{
		}
		
		void build_in_background_if_needed(dispatch_group_t group, dispatch_queue_t queue)
		{
			// Use dispatch only if needed, i.e. the destination needs to be compressed
			// or we need to build rank/select/bp support.
			if constexpr (!USES_FAST_PATH)
			{
				dispatch_group_async(group, queue, ^{
					static_assert(is_nonconst_reference_v <decltype(src)>);
					static_assert(is_nonconst_reference_v <decltype(dst)>);
				
					if constexpr (DESTINATION_IS_UNCOMPRESSED)
						dst = std::move(src);
					else
						dst = t_dst_bit_vector(src);
				
					std::apply([this](auto & ... support_ds){
						(prepare_support(dst, support_ds), ...);
					}, support);
				});
			}
		}
		
		void move_if_needed()
		{
			if constexpr (USES_FAST_PATH)
				dst = std::move(src);
		}
	};
	
	
	auto bit_vector_builders_from_state(
		bedinx_vector_builder_state &state,
		path_index_support_base &pi_support
	)
	{
		// X handled separately.
		
		return std::make_tuple(
			bit_vector_builder(state.b,  pi_support.b,  std::tie(pi_support.b_rank1_support, pi_support.b_select1_support)),
			bit_vector_builder(state.e,  pi_support.e,  std::tie(pi_support.e_select1_support)),
			bit_vector_builder(state.d,  pi_support.d,  std::tie(pi_support.d_rank1_support)),
			bit_vector_builder(state.i,  pi_support.i,  std::tie(pi_support.i_rank1_support)),
			bit_vector_builder(state.bh, pi_support.bh, std::tie(pi_support.bh_rank1_support, pi_support.bh_select0_support)),
			bit_vector_builder(state.m,  pi_support.m,  std::tie(pi_support.m_select1_support)),
			bit_vector_builder(state.u,  pi_support.u,  std::tie())
		);
	}
	
	
	// We would like that concurrent_builder’s constructors need not be rewritten.
	struct bedinx_vector_builder_helper
	{
		lb::dispatch_ptr <dispatch_group_t>	m_group;
		lb::dispatch_semaphore_lock			m_position_block_vector_mutex;
		
		bedinx_vector_builder_helper():
			m_group(dispatch_group_create()),
			m_position_block_vector_mutex(1)
		{
		}
	};
	
	
	class bedinx_vector_builder final : public concurrent_builder <bedinx_values_buffer, bedinx_vector_builder_state &>,
	                                    private bedinx_vector_builder_helper 
	{
		friend lb::function_output_iterator <bedinx_vector_builder>;
		
	public:
		using concurrent_builder <bedinx_values_buffer, bedinx_vector_builder_state &>::concurrent_builder;
		
		void process(std::size_t const pos, std::size_t const length, buffer_type &dst) override
		{
			bedinx_set_positions_for_range <path_index_support_base::U_BV_BLOCK_SIZE>(m_csa, m_reverse_csa, m_graph, pos, pos + length, dst);
		}
		
		void postprocess(std::size_t const pos, std::size_t const length, buffer_type &buffer) override;
		
	protected:
		void convey_and_merge_bnx_values_wt(position_block &&pb);
		void merge_position_blocks_wt(position_block_vector_type &&position_blocks);
	};


	void bedinx_vector_builder::merge_position_blocks_wt(position_block_vector_type &&position_blocks)
	{
#ifndef NDEBUG
		for (auto const &pb : position_blocks)
			libbio_assert(std::is_sorted(pb.b_pos.begin(), pb.b_pos.end()));
#endif
		
		// Sort so that blocks of similar size are merged.
		std::sort(position_blocks.begin(), position_blocks.end(), [](position_block const &lhs, position_block const &rhs){
			return lhs.b_pos.size() < rhs.b_pos.size();
		});
		
		// Pairwise merge.
		auto concurrent_queue(get_concurrent_queue());
		auto group(get_builder_group());
		auto const merged_block_count(position_blocks.size());
		libbio_assert_eq(0, merged_block_count % 2);
		for (std::size_t i(0); i < merged_block_count; i += 2)
		{
			lb::dispatch_group_async_fn(group, concurrent_queue, [this, lhs = std::move(position_blocks[i]), rhs = std::move(position_blocks[i + 1])](){
				
				position_block dst_pb(lhs.b_pos.size() + rhs.b_pos.size(), lhs.b_pos.width(), lhs.n.width(), lhs.x.width());
				lb::function_output_iterator merge_it(dst_pb);
				ranges::merge(lhs.zip_view(), rhs.zip_view(), merge_it);
				
				libbio_assert(std::is_sorted(dst_pb.b_pos.begin(), dst_pb.b_pos.end()));
				convey_and_merge_bnx_values_wt(std::move(dst_pb));
			});
		}
	}
	
	
	void bedinx_vector_builder::convey_and_merge_bnx_values_wt(position_block &&pb)
	{
		// The values in the position block are not guaranteed to be sorted,
		// since they can originate from multiple blocks. This could be solved
		// by using one set of buffers per block. Instead, we just sort them here.
		pb.sort();
		
		libbio_assert(std::is_sorted(pb.b_pos.begin(), pb.b_pos.end()));

		using std::swap;
		
		position_block_vector_type position_blocks;
		std::size_t block_count{};
		
		{
			// Critical section.
			{
				std::lock_guard <lb::dispatch_semaphore_lock> const lock((m_position_block_vector_mutex));
				
				block_count = m_state.position_blocks.size();
				if (0 == block_count)
				{
					m_state.position_blocks.emplace_back(std::move(pb));
					return;
				}
				
				// Get the blocks for merging.
				swap(position_blocks, m_state.position_blocks);
				if (0 == block_count % 2)
					m_state.position_blocks.emplace_back(std::move(pb));
			}
			
			// No need to do this inside the critical section.
			if (block_count % 2)
				position_blocks.emplace_back(std::move(pb));
		}
		
		merge_position_blocks_wt(std::move(position_blocks));
	}
	
	
	void bedinx_vector_builder::postprocess(std::size_t const pos, std::size_t const length, buffer_type &buffer)
	{
		auto concurrent_queue(get_concurrent_queue());
		auto group(*m_group);
		
		dispatch_group_async(group, concurrent_queue, ^{
			position_block pb(buffer.b_positions, buffer.block_numbers, buffer.shortest_prefix_lengths); // Replaces block_numbers and shortest_prefix_lengths.
			convey_and_merge_bnx_values_wt(std::move(pb));
		});
		
		dispatch_group_async(group, concurrent_queue, ^{
			auto const &first_block(m_graph.blocks[pos]);
			auto const &end_block(m_graph.blocks[pos + length]);
			auto const bit_offset(first_block.node_csum * u_row_size <path_index_support_base::U_BV_BLOCK_SIZE>(m_graph));
			
			//std::cerr << "assign: " << (&buffer.u_values) << " bit_offset: " << bit_offset << '\n';
			assign_bv_contents(buffer.u_values, m_state.u, bit_offset);
		});
		
		dispatch_group_async(group, concurrent_queue, ^{
			for (auto const b_pos : buffer.b_positions)
			{
				libbio_assert_lt(b_pos, m_state.b.size());
				m_state.b[b_pos] = 1;
			}
		});
		
		dispatch_group_async(group, concurrent_queue, ^{
			for (auto const e_pos : buffer.e_positions)
			{
				libbio_assert_lt(e_pos, m_state.e.size());
				m_state.e[e_pos] = 1;
			}
		});
		
		dispatch_group_async(group, concurrent_queue, ^{
			for (auto const d_pos : buffer.d_positions)
			{
				libbio_assert_lt(d_pos, m_state.d.size());
				m_state.d[d_pos] = 1;
			}
		});
		
		dispatch_group_async(group, concurrent_queue, ^{
			for (auto const i_pos : buffer.i_positions)
			{
				libbio_assert_lt(i_pos, m_state.i.size());
				m_state.i[i_pos] = 1;
			}
		});
		
		dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
		
		// The buffer will be relinquished here.
	}
	
	
	class alr_vector_builder final : public concurrent_builder <alr_values_buffer> 
	{
		using concurrent_builder <alr_values_buffer>::concurrent_builder;
		
		void process(std::size_t const pos, std::size_t const length, buffer_type &dst) override
		{
			alr_values_for_range(m_csa, m_reverse_csa, m_graph, m_support.d_rank1_support, pos, pos + length, dst);
		}
		
		void postprocess(std::size_t const pos, std::size_t const length, buffer_type &src) override
		{
			auto range(rsv::zip(src.alpha_values, src.alpha_tilde_values, src.a_values, src.a_tilde_values, src.lr_values));
			for (auto const &[alpha_val, alpha_tilde_val, a_val, a_tilde_val, lr_val] : range)
			{
				libbio_assert_lt(std::uint64_t(alpha_val), m_support.a.size());
				libbio_assert_lt(std::uint64_t(alpha_val), m_support.r.size());
				libbio_assert_lt(std::uint64_t(alpha_tilde_val), m_support.l.size());
				libbio_assert_lt(std::uint64_t(alpha_tilde_val), m_support.a_tilde.size());
				
				assign_value(m_support.a, alpha_val, a_val);
				assign_value(m_support.a_tilde, alpha_tilde_val, a_tilde_val);
				assign_value(m_support.l, alpha_tilde_val, lr_val);
				assign_value(m_support.r, alpha_val, lr_val);
			}
		}
	};
}


namespace founder_graphs::founder_graph_indices {
	
	void dispatch_concurrent_builder::build_supporting_data_structures(
		block_graph const &gr,
		csa_type const &csa,
		reverse_csa_type const &reverse_csa,
		path_index_support &support,
		dispatch_concurrent_builder_delegate &delegate
	)
	{
		// Most of the objects in this namespace are non-owning. Since they also push tasks
		// to a concurrent queue, they need to be kept in memory at least until the next call
		// to dispatch_group_wait.
		namespace dcbs = dispatch_concurrent_builder_support;
		
		auto const block_count(gr.blocks.size() - 1); // The last block is a sentinel.
		auto const u_row_size(u_row_size <path_index_support_base::U_BV_BLOCK_SIZE>(gr));
		auto const u_size(gr.node_count * u_row_size);
		auto const csa_size(csa.size());
		libbio_assert_eq(csa_size, reverse_csa.size());
		auto const csa_size_bits(lb::bits::highest_bit_set(csa_size));
		auto const block_number_bits(lb::bits::highest_bit_set(block_count));
		auto const node_label_max_length_bits(lb::bits::highest_bit_set(gr.node_label_max_length));
		auto const bits_h(lb::bits::highest_bit_set(gr.max_block_height));
		auto const bits_2h(lb::bits::highest_bit_set(2 * gr.max_block_height));
		
		auto const block_number_max(max_value_for_bits <std::uint64_t>(block_number_bits));
		auto const max_h(max_value_for_bits <std::uint64_t>(bits_h));
		auto const max_2h(max_value_for_bits <std::uint64_t>(bits_2h));
		
		auto const alpha_tilde_count(2 + gr.blocks.front().segments.size() + gr.edge_count);
		auto const alpha_bits(lb::bits::highest_bit_set(gr.edge_count - 1));
		auto const alpha_tilde_bits(lb::bits::highest_bit_set(alpha_tilde_count - 1));
		
		delegate.reading_bit_vector_values();
		
		support.input_count = gr.input_count;
		support.u_row_size = u_row_size;
		
		{
			dcbs::bedinx_vector_builder_state support_state(
				csa_size,
				1 + gr.node_count + gr.node_label_length_sum,
				1 + gr.blocks.size() + gr.node_count,
				gr.aligned_size,
				u_size
			);
			auto &support_state_ref(support_state);
			
			// B (bh) and M (m), memory for A, Ã, L’ and R’.
			lb::dispatch_group_async_fn(
				*m_group,
				*m_concurrent_queue,
				[&gr, &support, &support_state, bits_h, max_h, bits_2h, max_2h, alpha_tilde_count, block_count](){
				
					// These are only needed after the first dispatch_group_wait().
					{
						support.a.width(bits_h);
						support.a_tilde.width(bits_h);
						support.a.assign(gr.edge_count, max_h);
						support.a_tilde.assign(alpha_tilde_count, max_h);
						
						support.l.width(bits_2h);
						support.r.width(bits_2h);
						support.l.assign(alpha_tilde_count, max_2h);
						support.r.assign(gr.edge_count, max_2h);
					}
				
					support_state.bh[0] = 0;
					std::size_t height_sum(1);
					for (std::size_t i(0); i < block_count; ++i)
					{
						auto const &block(gr.blocks[i]);
						auto const height(block.segments.size());
						height_sum += height;
						support_state.bh[height_sum] = 0;
						++height_sum;
						
						support_state.m[block.aligned_position] = 1;
					}
				}
			);
			
			// ℬ, ℰ, D, I, N, X, U.
			dcbs::bedinx_vector_builder bedinx_vector_builder(
				csa,
				reverse_csa,
				gr,
				*this,
				support,
				support_state
			);
			
			for (std::size_t i(0); i < block_count; i += m_chunk_size)
			{
				dispatch_semaphore_wait(*m_sema, DISPATCH_TIME_FOREVER);
				
				// Determine the chunk size and the number of nodes in the chunk.
				auto const chunk_size(std::min(block_count - i, m_chunk_size));
				bedinx_vector_builder.handle_range(i, chunk_size);
			}
			
			// Wait for the tasks to complete.
			dispatch_group_wait(*m_group, DISPATCH_TIME_FOREVER);
			
			delegate.processing_bit_vector_values();
			
			libbio_assert_eq(1, support_state.position_blocks.size());
			auto &first_position_block(support_state.position_blocks.front());
			auto const &x_values(first_position_block.x);
			
			// Prepare X with the calculated positions and its rank and select support.
			dispatch_group_async(*m_group, *m_concurrent_queue, ^{
				static_assert(is_const_reference_v <decltype(x_values)>);
				static_assert(is_nonconst_reference_v <decltype(support_state_ref)>);
				support_state_ref.x[0] = 0;
				std::size_t length_sum(1);
				for (auto const length : x_values)
				{
					length_sum += length;
					support_state_ref.x[length_sum] = 0;
					++length_sum;
				}
				
				typedef std::decay_t <decltype(support.x)> x_vector_type;
				if constexpr (std::is_same_v <sdsl::bit_vector, x_vector_type>)
					support.x = std::move(support_state_ref.x);
				else
					support.x = x_vector_type(support_state_ref.x);
				
				dispatch_group_async(*m_group, *m_concurrent_queue, ^{
					static_assert(is_nonconst_reference_v <decltype(support)>);
					prepare_support(support.x, support.x_rank1_support);
				});
				
				dispatch_group_async(*m_group, *m_concurrent_queue, ^{
					static_assert(is_nonconst_reference_v <decltype(support)>);
					prepare_support(support.x, support.x_select0_support);
				});
			});
			
			// Compress U and the other vectors.
			// bv_builders needs to be in the same or enclosing (not nested) block w.r.t. the next call to dispatch_group_wait().
			auto bv_builders(bit_vector_builders_from_state(support_state, support));
			std::apply([this](auto & ... builder){
				(builder.build_in_background_if_needed(*m_group, *m_concurrent_queue), ...);	// Slow path if needed.
			}, bv_builders);
			std::apply([this](auto & ... builder){
				(builder.move_if_needed(), ...);												// Fast path if possible.
			}, bv_builders);
			
			// Move N.
			support.n = std::move(first_position_block.n);
			
			// Wait.
			dispatch_group_wait(*m_group, DISPATCH_TIME_FOREVER);
		}
		
		// A, Ã, L’ and R’.
		delegate.filling_integer_vectors();
		{
			dcbs::alr_vector_builder alr_vector_builder(
				csa,
				reverse_csa,
				gr,
				*this,
				support,
				alr_values_buffer(alpha_bits, alpha_tilde_bits, bits_h, bits_2h)
			);
			
			// The first block must not be given.
			for (std::size_t i(1); i < block_count; i += m_chunk_size)
			{
				dispatch_semaphore_wait(*m_sema, DISPATCH_TIME_FOREVER);
				
				// Determine the chunk size and the number of nodes in the chunk.
				auto const chunk_size(std::min(block_count - i, m_chunk_size));
				alr_vector_builder.handle_range(i, chunk_size);
			}
			
			// Wait.
			dispatch_group_wait(*m_group, DISPATCH_TIME_FOREVER);
		}
	}
}
