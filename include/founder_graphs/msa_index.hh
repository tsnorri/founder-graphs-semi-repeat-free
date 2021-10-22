/*
 * Copyright (c) 2021 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_MSA_INDEX_HH
#define FOUNDER_GRAPHS_MSA_INDEX_HH

#include <istream>
#include <ostream>
#include <sdsl/bit_vectors.hpp>
#include <sdsl/rank_support_v5.hpp>
#include <sdsl/select_support_mcl.hpp>
#include <sdsl/rrr_vector.hpp>
#include <vector>


namespace founder_graphs {
	
	struct aligned_sequence_index
	{
		//typedef sdsl::bit_vector				bit_vector_type;
		//typedef sdsl::rank_support_v5 <0>		rank0_support_type;
		//typedef sdsl::select_support_mcl <0>	select0_support_type;
		typedef sdsl::rrr_vector <>				bit_vector_type;
		typedef bit_vector_type::rank_0_type	rank0_support_type;
		typedef bit_vector_type::select_0_type	select0_support_type;
		
		bit_vector_type			gap_positions;
		rank0_support_type		rank0_support;
		select0_support_type	select0_support;
		
		aligned_sequence_index() = default;
		
#if 0
		explicit aligned_sequence_index(std::size_t const size):
			gap_positions(size)
		{
		}
#endif

		explicit aligned_sequence_index(sdsl::bit_vector const &vec):
			gap_positions(vec)
		{
		}
		
		inline void prepare_rank_and_select_support();
		
		template <typename t_archive>
		void CEREAL_SAVE_FUNCTION_NAME(t_archive &archive) const;
		
		template <typename t_archive>
		void CEREAL_LOAD_FUNCTION_NAME(t_archive &archive);
	};
	
	
	struct msa_index
	{
		typedef std::vector <aligned_sequence_index>	index_vector;
		
		index_vector	sequence_indices;
		
		template <typename t_archive>
		void serialize(t_archive &archive);
	};
	
	
	void aligned_sequence_index::prepare_rank_and_select_support()
	{
		rank0_support = rank0_support_type(&gap_positions);
		select0_support = select0_support_type(&gap_positions);
	}
	
	
	template <typename t_archive>
	void aligned_sequence_index::CEREAL_SAVE_FUNCTION_NAME(t_archive &archive) const
	{
		archive(CEREAL_NVP(gap_positions));
		archive(CEREAL_NVP(rank0_support));
		archive(CEREAL_NVP(select0_support));
	}
	
	
	template <typename t_archive>
	void aligned_sequence_index::CEREAL_LOAD_FUNCTION_NAME(t_archive &archive)
	{
		archive(CEREAL_NVP(gap_positions));
		archive(CEREAL_NVP(rank0_support));
		archive(CEREAL_NVP(select0_support));
		rank0_support.set_vector(&gap_positions);
		select0_support.set_vector(&gap_positions);
	}
	
	
	template <typename t_archive>
	void msa_index::serialize(t_archive &archive)
	{
		archive(CEREAL_NVP(sequence_indices));
	}
}

#endif
