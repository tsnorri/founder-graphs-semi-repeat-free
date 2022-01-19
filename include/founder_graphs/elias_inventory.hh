/*
 * Copyright (c) 2022 Tuukka Norri
 * This code is licensed under MIT license (see LICENSE for details).
 */

#ifndef FOUNDER_GRAPHS_ELIAS_INVENTORY_HH
#define FOUNDER_GRAPHS_ELIAS_INVENTORY_HH

#include <cstddef>
#include <cstdint>
#include <libbio/assert.hh>
#include <range/v3/range/primitives.hpp>	// ranges::size
#include <range/v3/view/enumerate.hpp>		// ranges::views::enumerate
#include <sdsl/int_vector.hpp>
#include <sdsl/select_support_mcl.hpp>


namespace founder_graphs {
	
	class elias_inventory_base
	{
	public:
		typedef sdsl::int_vector <0>			int_vector_type;
		typedef sdsl::select_support_mcl <1>	select1_support_type;
		typedef int_vector_type::value_type		remainder_value_type;
		
	protected:
		sdsl::bit_vector		m_quotients;
		int_vector_type			m_remainders;
	};
	
	
	class elias_inventory : public elias_inventory_base
	{
	public:
		typedef std::uint64_t	value_type;
		
	protected:
		select1_support_type	m_quotient_select1_support;
		
	protected:
		static inline remainder_value_type low_bit_mask(std::uint8_t const low_bits);
		
	public:
		elias_inventory() = default;
		
		template <typename t_range>
		elias_inventory(t_range &&range, std::uint8_t const low_bits);
		
		elias_inventory(elias_inventory const &other):
			elias_inventory_base(other),
			m_quotient_select1_support(&m_quotients)
		{
		}
		
		elias_inventory(elias_inventory &&other):
			elias_inventory_base(std::move(other)),
			m_quotient_select1_support(&m_quotients)
		{
		}
			
		inline elias_inventory &operator=(elias_inventory const &other) &;
		inline elias_inventory &operator=(elias_inventory &&other) &;
		
		inline value_type operator[](std::size_t const idx) const;
		
		template <typename t_archive>
		void CEREAL_SAVE_FUNCTION_NAME(t_archive &archive) const;
		
		template <typename t_archive>
		void CEREAL_LOAD_FUNCTION_NAME(t_archive &archive);
	};
	
	
	auto elias_inventory::low_bit_mask(std::uint8_t const low_bits) -> remainder_value_type
	{
		remainder_value_type retval{};
		retval = ~retval;
		libbio_assert_lt(low_bits, CHAR_BIT * sizeof(remainder_value_type));
		retval >>= CHAR_BIT * sizeof(remainder_value_type) - low_bits;
		return retval;
	}
	
	
	template <typename t_range>
	elias_inventory::elias_inventory(t_range &&range, std::uint8_t const low_bits)
	{
		auto const size(ranges::size(range));
		m_remainders = int_vector_type(size, 0, low_bits);
		
		auto const mask(low_bit_mask(low_bits));
		std::size_t quotient_bits_needed{};
		
		// FIXME: determine the quotient type from the range.
		{
			std::size_t prev_value{};
			std::size_t prev_quotient{};
			for (auto const val : range)
			{
				libbio_assert_lte(prev_value, val);
				
				auto const quotient(val >> low_bits);
				libbio_assert_lte(prev_quotient, quotient);
				quotient_bits_needed += 1 + (quotient - prev_quotient);
				
				prev_value = val;
			}
		}
		
		m_quotients.resize(quotient_bits_needed, 0);
		
		{
			std::size_t prev_quotient{};
			for (auto const [i, val] : ranges::views::enumerate(range))
			{
				auto const quotient(val >> low_bits);
				auto const remainder(val & mask);
				auto const diff(quotient - prev_quotient);
				
				m_quotients[1 + diff] = 1;
				m_remainders[i] = remainder;
			}
		}
		
		m_quotient_select1_support = select1_support_type(&m_quotients);
	}
	
	
	inline elias_inventory &elias_inventory::operator=(elias_inventory const &other) &
	{
		if (this != &other)
		{
			elias_inventory_base::operator=(other);
			m_quotient_select1_support = other.m_quotient_select1_support;
			m_quotient_select1_support.set_vector(&m_quotients);
		}
		return *this;
	}
	
	
	inline elias_inventory &elias_inventory::operator=(elias_inventory &&other) &
	{
		if (this != &other)
		{
			elias_inventory_base::operator=(std::move(other));
			m_quotient_select1_support = std::move(other.m_quotient_select1_support);
			m_quotient_select1_support.set_vector(&m_quotients);
		}
		return *this;
	}
	
	
	inline auto elias_inventory::operator[](std::size_t const idx) const -> value_type
	{
		libbio_assert_lt(idx, m_remainders.size());
		auto const low_bits(m_remainders.width());
		value_type retval(m_remainders[idx]);
		value_type const high_val(m_quotient_select1_support(1 + idx) - idx);
		retval |= high_val << low_bits;
		return retval;
	}
	
	
	template <typename t_archive>
	void elias_inventory::CEREAL_SAVE_FUNCTION_NAME(t_archive &archive) const
	{
		archive(CEREAL_NVP(m_quotients));
		archive(CEREAL_NVP(m_remainders));
		archive(CEREAL_NVP(m_quotient_select1_support));
	}
	
	
	template <typename t_archive>
	void elias_inventory::CEREAL_LOAD_FUNCTION_NAME(t_archive &archive)
	{
		archive(CEREAL_NVP(m_quotients));
		archive(CEREAL_NVP(m_remainders));
		archive(CEREAL_NVP(m_quotient_select1_support));
		m_quotient_select1_support.set_vector(&m_quotients);
	}
}

#endif
