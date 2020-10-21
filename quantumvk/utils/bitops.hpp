#pragma once

#ifdef _MSC_VER
#include <intrin.h>
#endif

//Helpers for bit operations
namespace Util
{
#ifdef __GNUC__
#define leading_zeroes(x) ((x) == 0 ? 32 : __builtin_clz(x))
#define trailing_zeroes(x) ((x) == 0 ? 32 : __builtin_ctz(x))
#define trailing_ones(x) __builtin_ctz(~uint32_t(x))
#elif defined(_MSC_VER)
	namespace Internal
	{
		static inline uint32_t clz(uint32_t x)
		{
			unsigned long result;
			if (_BitScanReverse(&result, x))
				return 31 - result;
			else
				return 32;
		}

		static inline uint32_t ctz(uint32_t x)
		{
			unsigned long result;
			if (_BitScanForward(&result, x))
				return result;
			else
				return 32;
		}
	}

#define leading_zeroes(x) ::Util::Internal::clz(x)
#define trailing_zeroes(x) ::Util::Internal::ctz(x)
#define trailing_ones(x) ::Util::Internal::ctz(~uint32_t(x))
#else
#error "Implement me."
#endif

	// Searches through the bitmask "value", from least signifigant to most and for every 1 set in the bitmask, calls func on the position of that 1.
	template <typename T>
	inline void ForEachBit(uint32_t value, const T& func)
	{
		while (value)
		{
			uint32_t bit = trailing_zeroes(value);
			func(bit);
			value &= ~(1u << bit);
		}
	}

	template <typename T>
	inline void ForEachBitRange(uint32_t value, const T& func)
	{
		if (value == ~0u)
		{
			func(0, 32);
			return;
		}

		uint32_t bit_offset = 0;
		while (value)
		{
			uint32_t bit = trailing_zeroes(value);
			bit_offset += bit;
			value >>= bit;
			uint32_t range = trailing_ones(value);
			func(bit_offset, range);
			value &= ~((1u << range) - 1);
		}
	}

	inline uint32_t NextPow2(uint32_t v)
	{
		v--;
		v |= v >> 16;
		v |= v >> 8;
		v |= v >> 4;
		v |= v >> 2;
		v |= v >> 1;
		return v + 1;
	}

	inline uint32_t GetMostSignificantBitSet(uint32_t value)
	{
		if (!value)
			return 32;
		
		return 31 - leading_zeroes(value);
	}
}