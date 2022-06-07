/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_FOUNDER_GRAPH_INDICES_PATH_INDEX_HH
#define FOUNDER_GRAPHS_FOUNDER_GRAPH_INDICES_PATH_INDEX_HH

#include <cereal/macros.hpp>
#include <founder_graphs/founder_graph_indices/basic_types.hh>
#include <founder_graphs/utility.hh>
#include <libbio/bits.hh>
#include <sdsl/int_vector.hpp>
#include <sdsl/rrr_vector.hpp>


namespace founder_graphs::founder_graph_indices {
	
	// m and t are not described in the dissertation. They are needed for locate queries such that aligned co-ordinates of
	// sufficiently long matches may be retrieved. Suppose the lexicographic range needs to be extended, i.e. backward-searching
	// the next character to the left in the pattern would yield the empty range but the next character to the left in the BWT
	// of the indexed text is #; then we must be at a block boundary. We can then determine the leftmost aligned position of
	// the block in question by determining the number of the first segment of the block (in ϱ order) and finding its aligned
	// co-ordinate from m. (Just the positions need be stored instead of the cumulative sum of the lengths b.c. all the lengths
	// are greater than zero.)
	
	// For getting automatically-generated copy and move constructor and assignment operator.
	struct path_index_support_base
	{
		constexpr static inline const std::uint16_t BV_BLOCK_SIZE{15};
		constexpr static inline const std::uint16_t U_BV_BLOCK_SIZE{63};
		
		typedef sdsl::rrr_vector <BV_BLOCK_SIZE>	bit_vector_type;
		typedef bit_vector_type						d_bit_vector_type; // Needed for index_construction.hh.
		typedef sdsl::rrr_vector <U_BV_BLOCK_SIZE>	u_bit_vector_type;
		
		template <std::uint8_t t_i>
		using rank_support_type = founder_graphs::founder_graph_indices::rank_support_type <bit_vector_type, t_i>;
		
		template <std::uint8_t t_i>
		using select_support_type = founder_graphs::founder_graph_indices::select_support_type <bit_vector_type, t_i>;
		
		bit_vector_type			b;					// Shortest prefix lexicographic range right bounds, ℬ in the dissertation.
		bit_vector_type			e;					// Shortest prefix lexicographic range right bounds, ℰ in the dissertation.
		d_bit_vector_type		d;					// d[i] = 1 ⇔ i is the lexicographic rank of some l(v)l(w)#.
		bit_vector_type			i;					// i[l] = 1 ⇔ [l, r] is the co-lexicographic range of some l(v).
		bit_vector_type			x;					// Prefix lengths as unary in ℬ order.
		bit_vector_type			bh;					// Block heights as unary, B in the dissertation.
		bit_vector_type			m;					// m[i] = 1 ⇔ i is the aligned position of some block in block order (μήκος).
		sdsl::int_vector <0>	n;					// Block number for every such node where b[i] = 1 (νούμερο).
		sdsl::int_vector <0>	a;					// Given an edge, rank of the source node in its block using the value of α as the key.
		sdsl::int_vector <0>	a_tilde;			// Given an edge, rank of the destination node in its block using the value of α̃ as the key.
		sdsl::int_vector <0>	l;					// For every (v, w) ∈ E, ϱ(w) - ϱ(v) using α̃((v, w)) as the key.
		sdsl::int_vector <0>	r;					// Same but using α((v, w)) as the key.
		u_bit_vector_type		u;
		rank_support_type <1>	b_rank1_support;
		rank_support_type <1>	d_rank1_support;
		rank_support_type <1>	i_rank1_support;
		rank_support_type <1>	x_rank1_support;
		rank_support_type <1>	bh_rank1_support;
		select_support_type <1>	b_select1_support;
		select_support_type <1>	e_select1_support;
		select_support_type <0>	x_select0_support;
		select_support_type <0>	bh_select0_support;
		select_support_type <1>	m_select1_support;
		count_type				input_count{};
		count_type				u_row_size{};
		
		path_index_support_base() = default;
		path_index_support_base(path_index_support_base const &) = default;
		path_index_support_base(path_index_support_base &&) = default;
		path_index_support_base &operator=(path_index_support_base const &) & = default;
		path_index_support_base &operator=(path_index_support_base &&) & = default;
	};
	
	
	struct path_index_support final : public path_index_support_base
	{
		friend std::uint64_t
		sdsl_serialize <>(path_index_support const &, std::string const &, sdsl::structure_tree_node *, std::ostream &);
		
	public:
		typedef csa_type::size_type				size_type;
		static_assert(std::is_same_v <size_type, std::uint64_t>); // Not sure if this is needed.
		
	protected:
		inline void update_support();
		
		template <typename t_index, typename t_visitor>
		static void visit_members(t_index &idx, t_visitor &visitor);
		
	public:
		path_index_support() = default;
		
		path_index_support(path_index_support const &other):
			path_index_support_base(other)
		{
			update_support();
		}
		
		path_index_support(path_index_support &&other):
			path_index_support_base(std::move(other))
		{
			update_support();
		}
		
		inline path_index_support &operator=(path_index_support const &other) &;
		inline path_index_support &operator=(path_index_support &&other) &;
		
		template <typename t_archive>
		void CEREAL_SAVE_FUNCTION_NAME(t_archive &archive) const;
		
		template <typename t_archive>
		void CEREAL_LOAD_FUNCTION_NAME(t_archive &archive);
		
		size_type serialize(std::ostream &out, sdsl::structure_tree_node *v = nullptr, std::string name = "") const;
	};
	
	
	class path_index
	{
		friend std::uint64_t
		sdsl_serialize <>(path_index const &, std::string const &, sdsl::structure_tree_node *, std::ostream &);
		
	public:
		typedef path_index_support::size_type	size_type;
		
	protected:
		csa_type				m_csa;
		reverse_csa_type		m_reverse_csa;
		path_index_support		m_s;
		
	protected:
		template <typename t_index, typename t_visitor>
		static void visit_members(t_index &idx, t_visitor &visitor);
		
	public:
		path_index() = default;
		
		path_index(csa_type &&csa, reverse_csa_type &&reverse_csa, path_index_support &&support):
			m_csa(std::move(csa)),
			m_reverse_csa(std::move(reverse_csa)),
			m_s(std::move(support))
		{
		}
		
		csa_type const &get_csa() const { return m_csa; }
		reverse_csa_type const &get_reverse_csa() const { return m_reverse_csa; }
		path_index_support const &get_support() const { return m_s; }
		count_type get_input_count() const { return m_s.input_count; }
		
		void set_csa(csa_type const &csa) { m_csa = csa; }
		void set_csa(csa_type &&csa) { m_csa = std::move(csa); }
		void set_reverse_csa(reverse_csa_type const &reverse_csa) { m_reverse_csa = reverse_csa; }
		void set_reverse_csa(reverse_csa_type &&reverse_csa) { m_reverse_csa = std::move(reverse_csa); }
		void set_support(path_index_support const &s) { m_s = s; }
		void set_support(path_index_support &&s) { m_s = std::move(s); }
		
		// FIXME: check that t_out_it is an output iterator (at least by some definition).
		template <typename t_it, typename t_out_it>
		auto list_occurrences(
			t_it begin,
			t_it it,
			length_type &block_aln_pos,
			length_type &offset,
			t_out_it occ_it
		) const -> std::pair <size_type, bool>;
		
		// FIXME: check that t_out_it is an output iterator (at least by some definition).
		template <typename t_it, typename t_out_it>
		auto list_occurrences(
			t_it begin,
			t_it it,
			length_type &block_aln_pos,
			length_type &offset,
			t_out_it occ_it,
			sdsl::bit_vector &occ_buffer,
			sdsl::bit_vector &head_buffer,
			sdsl::bit_vector &tail_buffer
		) const -> std::pair <size_type, bool>;
		
		template <typename t_archive>
		void CEREAL_SAVE_FUNCTION_NAME(t_archive &archive) const;
		
		template <typename t_archive>
		void CEREAL_LOAD_FUNCTION_NAME(t_archive &archive);
		
		size_type serialize(std::ostream &out, sdsl::structure_tree_node *v = nullptr, std::string name = "") const;
		
	protected:
		template <typename t_process_fn, typename t_ret_fold_fn = const_op <std::uint64_t>, typename t_fold_fn = head_op <std::uint64_t>, typename ... t_idxs>
		std::uint64_t process_u(sdsl::bit_vector &dst, t_idxs ... node_idx) const
			requires (std::is_convertible_v <t_idxs, size_type> && ...);
		
		inline bool expand_lexicographic_range(size_type const lb, size_type const rb, size_type &lc, size_type &nlb, size_type &nrb) const;
		inline size_type find_lhs_node(size_type const lb, size_type const block_number) const;
		inline std::uint64_t check_node_paths(size_type const node_idx, sdsl::bit_vector &occ_buffer) const;
		
		void combine_node_paths_left(
			co_lexicographic_range const co_range,
			size_type const rhs_node_idx,
			sdsl::bit_vector &head_buffer
		) const;
		
		void combine_node_paths_left_multiple(
			co_lexicographic_range const co_range,
			size_type const prev_node_count,
			sdsl::bit_vector &head_buffer
		) const;
		
		void combine_node_paths_right(
			size_type const first_lb,
			size_type const first_rb,
			size_type const prev_node_count,
			sdsl::bit_vector &tail_buffer
		) const;
		
		template <typename t_out_it, typename ... t_bit_vectors>
		count_type report_matches(t_out_it occ_it, t_bit_vectors const & ... path_vectors) const
			requires (std::is_same_v <t_bit_vectors, sdsl::bit_vector> && ...);
	};
	
	
	void path_index_support::update_support()
	{
		b_rank1_support.set_vector(&b);
		d_rank1_support.set_vector(&d);
		i_rank1_support.set_vector(&i);
		x_rank1_support.set_vector(&x);
		bh_rank1_support.set_vector(&bh);
		b_select1_support.set_vector(&b);
		e_select1_support.set_vector(&e);
		x_select0_support.set_vector(&x);
		bh_select0_support.set_vector(&bh);
		m_select1_support.set_vector(&m);
	}
	
	
	auto path_index_support::operator=(path_index_support const &other) & -> path_index_support &
	{
		if (this != &other)
		{
			path_index_support_base::operator=(other);
			update_support();
		}
		return *this;
	}
	
	
	auto path_index_support::operator=(path_index_support &&other) & -> path_index_support &
	{
		if (this != &other)
		{
			path_index_support_base::operator=(std::move(other));
			update_support();
		}
		return *this;
	}
	
	
	template <typename t_index, typename t_visitor>
	void path_index::visit_members(t_index &idx, t_visitor &visitor)
	{
		visitor("csa", idx.m_csa);
		visitor("reverse_csa", idx.m_reverse_csa);
		visitor("support", idx.m_s);
	}
	
	
	template <typename t_index, typename t_visitor>
	void path_index_support::visit_members(t_index &idx, t_visitor &visitor)
	{
		visitor("ℬ", idx.b);
		visitor("ℰ", idx.e);
		visitor("D", idx.d);
		visitor("I", idx.i);
		visitor("X", idx.x);
		visitor("B", idx.bh);
		visitor("M", idx.m);
		visitor("N", idx.n);
		visitor("A", idx.a);
		visitor("Ã", idx.a_tilde);
		visitor("ℒ", idx.l);
		visitor("ℛ", idx.r);
		visitor("U", idx.u);
		visitor("ℬ_rank1_support", idx.b_rank1_support);
		visitor("D_rank1_support", idx.d_rank1_support);
		visitor("I_rank1_support", idx.i_rank1_support);
		visitor("X_rank1_support", idx.x_rank1_support);
		visitor("B_rank1_support", idx.bh_rank1_support);
		visitor("ℬ_select1_support", idx.b_select1_support);
		visitor("ℰ_select1_support", idx.e_select1_support);
		visitor("X_select0_support", idx.x_select0_support);
		visitor("B_select0_support", idx.bh_select0_support);
		visitor("M_select1_support", idx.m_select1_support);
		visitor("input_count", idx.input_count);
		visitor("u_row_size", idx.u_row_size);
	}
	
	
	template <typename t_archive>
	void path_index_support::CEREAL_SAVE_FUNCTION_NAME(t_archive &archive) const
	{
		cereal_load_save_visitor visitor(archive);
		visit_members(*this, visitor);
	}
	
	
	template <typename t_archive>
	void path_index_support::CEREAL_LOAD_FUNCTION_NAME(t_archive &archive)
	{
		cereal_load_save_visitor visitor(archive);
		visit_members(*this, visitor);
		update_support();
	}
	
	
	template <typename t_archive>
	void path_index::CEREAL_SAVE_FUNCTION_NAME(t_archive &archive) const
	{
		cereal_load_save_visitor visitor(archive);
		visit_members(*this, visitor);
	}
	
	
	template <typename t_archive>
	void path_index::CEREAL_LOAD_FUNCTION_NAME(t_archive &archive)
	{
		cereal_load_save_visitor visitor(archive);
		visit_members(*this, visitor);
	}
	
	
	// Process i.e. perform the given operation with the U vector slices at the given indices.
	template <typename t_process_fn, typename t_ret_fold_fn, typename t_fold_fn, typename ... t_idxs>
	std::uint64_t path_index::process_u(sdsl::bit_vector &dst, t_idxs ... node_idx) const
		requires (std::is_convertible_v <t_idxs, size_type> && ...)
	{
		// Read the blocks from support.u s.t. one std::uint64_t may be filled at a time.
		
		constexpr auto const COUNT(sizeof...(node_idx));
		
		static_assert(path_index_support::U_BV_BLOCK_SIZE < 64);
		static_assert(64 < 2 * path_index_support::U_BV_BLOCK_SIZE);
		
		t_process_fn process_fn;	// For updating dst.
		t_ret_fold_fn ret_fn;		// For updating the return value of this function.
		t_fold_fn fold_fn;			// For combining the values for multiple node indices.
		
		std::uint64_t retval{};
		
		// Calculate the slice starting positions in U.
		std::array u_positions{node_idx...};
		static_assert(u_positions.size() == COUNT);
		array_apply(u_positions, [this](auto &val){ val *= m_s.u_row_size; });
		
		inline_for <COUNT>([&u_positions](auto const idx){
			libbio_assert_eq(0, std::get <idx()>(u_positions) % path_index_support::U_BV_BLOCK_SIZE);
		});
		
		// Pairs of words in the U slices.
		std::array <std::uint64_t, COUNT> lhsws;
		std::array <std::uint64_t, COUNT> rhsws;
		
		auto &u_pos(std::get <0>(u_positions)); // For checking the current position.
		auto const u_limit(u_pos + m_s.u_row_size);
		if (u_pos < u_limit) // Safety check.
		{
			std::uint64_t *dst_word(dst.data());
			
			// Read the initial blocks.
			inline_for <COUNT>([this, &u_positions, &lhsws](auto const idx){
				auto &u_pos(std::get <idx()>(u_positions));
				auto &lhsw(std::get <idx()>(lhsws));
			
				lhsw = m_s.u.get_int(u_pos, path_index_support::U_BV_BLOCK_SIZE);
				u_pos += path_index_support::U_BV_BLOCK_SIZE;
			});
				
			// Process rest of the blocks.
			std::uint8_t shift_amt(path_index_support::U_BV_BLOCK_SIZE);
			while (u_pos < u_limit)
			{
				// Check if we need to fill both the left and the right side words.
				if (0 == shift_amt)
				{
					shift_amt = path_index_support::U_BV_BLOCK_SIZE;
					inline_for <COUNT>([this, &u_positions, &lhsws](auto const idx){
						auto &u_pos(std::get <idx()>(u_positions));
						auto &lhsw(std::get <idx()>(lhsws));
					
						lhsw = m_s.u.get_int(u_pos, path_index_support::U_BV_BLOCK_SIZE);
						u_pos += path_index_support::U_BV_BLOCK_SIZE;
					});
					
					if (! (u_pos < u_limit))
						break;
				}
				
				// Fill the left side word with the least significant bits of the right side word.
				inline_for <COUNT>([this, &lhsws, &rhsws, u_pos, shift_amt](auto const idx){
					auto &lhsw(std::get <idx()>(lhsws));
					auto &rhsw(std::get <idx()>(rhsws));
				
					rhsw = m_s.u.get_int(u_pos, path_index_support::U_BV_BLOCK_SIZE);
					lhsw |= rhsw << shift_amt;
				});
				
				// Fold if needed, write to dst and update the return value.
				auto const fw(array_left_fold(lhsws, fold_fn));
				*dst_word = process_fn(*dst_word, fw);
				retval = ret_fn(retval, *dst_word);
				
				// Replace the left side word witht he most significant bits of the right side word.
				inline_for <COUNT>([this, &lhsws, &rhsws, &u_positions, shift_amt](auto const idx){
					auto &lhsw(std::get <idx()>(lhsws));
					auto &rhsw(std::get <idx()>(rhsws));
					auto &u_pos(std::get <idx()>(u_positions));
					lhsw = rhsw >> (path_index_support::U_BV_BLOCK_SIZE - shift_amt);
					u_pos += path_index_support::U_BV_BLOCK_SIZE;
				});
				
				++dst_word;
				--shift_amt;
			}
			
			if (shift_amt)
			{
				auto const fw(array_left_fold(lhsws, fold_fn));
				*dst_word = process_fn(*dst_word, fw);
				retval = ret_fn(retval, *dst_word);
			}
		}
		
		return retval;
	}
	
	
	bool path_index::expand_lexicographic_range(
		size_type const lb,
		size_type const rb,
		size_type &lc,
		size_type &nlb,
		size_type &nrb
	) const
	{
		auto const lc_(m_s.b_rank1_support(lb + 1));
		if (0 == lc_)
			return false;
		
		auto const nlb_(m_s.b_select1_support(lc_));
		auto const nrb_(m_s.e_select1_support(lc_));
		
		if (nlb_ <= lb && rb <= nrb_)
		{
			lc = lc_;
			nlb = nlb_;
			nrb = nrb_;
			return true;
		}
		
		return false;
	}
	
	
	auto path_index::find_lhs_node(size_type const lb, size_type const block_number) const -> size_type
	{
		// If lb is the left bound of the lexicographic range of some ℓ(v, w),
		// it is also the lexicographic rank of ℓ(v, w)# since # is lexicographically
		// smaller than any other character except $.
		auto const alpha(m_s.d_rank1_support(lb + 1) - 1);
		auto const node_rank_in_block(m_s.a[alpha]);
		auto const bh_pos(m_s.bh_select0_support(block_number + 1));
		auto const prev_node_count(m_s.bh_rank1_support(bh_pos));
		return prev_node_count + node_rank_in_block;
	}


	std::uint64_t path_index::check_node_paths(size_type const node_idx, sdsl::bit_vector &occ_buffer) const
	{
		return process_u <std::bit_and <std::uint64_t>, std::bit_or <std::uint64_t>>(occ_buffer, node_idx);
	}
	
	
	template <typename t_out_it, typename ... t_bit_vectors>
	auto path_index::report_matches(t_out_it occ_it, t_bit_vectors const & ... path_vectors) const -> count_type
		requires (std::is_same_v <t_bit_vectors, sdsl::bit_vector> && ...)
	{
		// Report the matches in O(m / 64 + occ) time.
		count_type retval{};
		std::array ptrs{path_vectors.data()...};
		static_assert(std::is_same_v <std::uint64_t const *, typename decltype(ptrs)::value_type>);
		
		count_type const word_count((parameter_pack_head(path_vectors...).bit_size() + 63U) >> 6U); // From bit_data_size() which is private.
		for (count_type i(0); i < word_count; ++i)
		{
			count_type seq_idx(count_type(64U) * i);
			std::uint8_t tz{};
			auto word(std::apply([i](auto const & ... bv_ptr){ return (bv_ptr[i] & ...); }, ptrs));
			
			// Find the next occurrence with a special instruction in O(1) time.
			// lb::trailing_zeros() returns 0 if word == 0 and otherwise the number of zeros plus one.
			while ((tz = libbio::bits::trailing_zeros(word)))
			{
				seq_idx += tz;
				*occ_it = seq_idx - 1; // Convert to 0-based.
				++occ_it;
				word >>= tz;
				++retval;
			}
		}
		
		return retval;
	}
	
	
	template <typename t_it, typename t_out_it>
	auto path_index::list_occurrences(
		t_it begin,
		t_it it,
		length_type &block_aln_pos,
		length_type &offset,
		t_out_it occ_it
	) const -> std::pair <size_type, bool>
	{
		sdsl::bit_vector occ_buffer;
		sdsl::bit_vector head_buffer;
		sdsl::bit_vector tail_buffer;
		return list_occurrences(begin, it, block_aln_pos, offset, occ_it, occ_buffer, head_buffer, tail_buffer);
	}
	
	
	template <typename t_it, typename t_out_it>
	auto path_index::list_occurrences(
		t_it begin,
		t_it it,
		length_type &block_aln_pos,
		length_type &offset,
		t_out_it occ_it,
		sdsl::bit_vector &occ_buffer,
		sdsl::bit_vector &head_buffer,
		sdsl::bit_vector &tail_buffer
	) const -> std::pair <size_type, bool>
	{
		auto const pattern_length(std::distance(begin, it));
		
		size_type first_lb{};
		size_type first_rb{m_csa.size() - 1};
		size_type first_block_number{};
		size_type first_b_rank{};
		size_type lb{};
		size_type rb{};
		
		{
			size_type count{};
			while (it != begin)
			{
				--it;
				count = sdsl::backward_search(m_csa, first_lb, first_rb, *it, first_lb, first_rb);
				if (!count)
					return {count, false};
				
				if (expand_lexicographic_range(first_lb, first_rb, first_b_rank, lb, rb))
				{
					first_block_number = m_s.n[first_b_rank - 1];
					goto did_expand_lexicographic_range;
				}
			}
			
			// Did iterate over the pattern, no expansion done.
			return {count, false};
		}
		
	did_expand_lexicographic_range:
		occ_buffer.assign(m_s.input_count, 1);
		size_type block_number{first_block_number - 1};
		size_type b_rank{};
		size_type next_lb{};
		size_type next_rb{};
		size_type node_label_length_1{};
		size_type node_label_length_2{};
		size_type node_idx{};
		while (it != begin)
		{
			--it;
			auto const count(sdsl::backward_search(m_csa, lb, rb, *it, lb, rb));
			if (!count)
				return {count, false};
			
			++node_label_length_1;
			if (expand_lexicographic_range(lb, rb, b_rank, next_lb, next_rb))
			{
				node_label_length_2 = node_label_length_1;
				node_label_length_1 = 0;
			
				node_idx = find_lhs_node(lb, block_number);
				if (!check_node_paths(node_idx, occ_buffer))
					return {0, false};
			
				lb = next_lb;
				rb = next_rb;
				--block_number;
			}
		}
		
		// Report the aligned position.
		block_aln_pos = m_s.m_select1_support(block_number + 2);
		offset = node_label_length_1;
		
		// Report the matching paths.
		{
			auto const first_block_bh_pos(m_s.bh_select0_support(first_block_number + 1));
			auto const first_block_prev_node_count(m_s.bh_rank1_support(first_block_bh_pos));
			combine_node_paths_right(first_lb, first_rb, first_block_prev_node_count, tail_buffer);
			if (node_label_length_2) // Did expand at least twice.
			{
				co_lexicographic_range co_range(m_reverse_csa);
				auto const count(co_range.forward_search_h(m_reverse_csa, begin, begin + node_label_length_1 + node_label_length_2));
				libbio_assert_lt(0, count);
				combine_node_paths_left(co_range, node_idx, head_buffer);
				return {report_matches(occ_it, occ_buffer, head_buffer, tail_buffer), true};
			}
			else
			{
				auto const expanded_label_length(m_s.x[first_b_rank - 1]);
				co_lexicographic_range co_range(m_reverse_csa);
				
				auto const seg_end_1(begin + node_label_length_1 + expanded_label_length - 1);
				auto const seg_end_2(begin + pattern_length);
				
				while (begin != seg_end_1)
				{
					auto const count(co_range.forward_search(m_reverse_csa, *begin));
					libbio_assert_lt(0, count);
					++begin;
				}
				
				size_type match_count(0);
				while (begin != seg_end_2)
				{
					auto const count(co_range.forward_search(m_reverse_csa, *begin));
					if (0 == count)
						break;
					
					auto co_range_(co_range);
					if (co_range_.forward_search(m_reverse_csa, '#'))
					{
						combine_node_paths_left_multiple(co_range_, first_block_prev_node_count, head_buffer);
						match_count += report_matches(occ_it, head_buffer, tail_buffer);
					}
				}
				
				return {match_count, true};
			}
		}
	}
}

#endif
