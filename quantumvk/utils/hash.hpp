#pragma once

#include <cstdint>
#include <string>

namespace Util
{
	using Hash = uint64_t;

	//Simple hasher class, able to hash, data, numerical values, pointers, and strings
	class Hasher
	{
	public:
		explicit Hasher(Hash h_)
			: h(h_)
		{
		}

		Hasher() = default;

		template <typename T>
		inline void data(const T* data_, size_t size)
		{
			size /= sizeof(T);
			for (size_t i = 0; i < size; i++)
				h = (h * 0x100000001b3ull) ^ data_[i];
		}

		inline void u32(uint32_t value)
		{
			h = (h * 0x100000001b3ull) ^ value;
		}

		inline void s32(int32_t value)
		{
			u32(uint32_t(value));
		}

		inline void f32(float value)
		{
			union
			{
				float f32;
				uint32_t u32;
			} u;
			u.f32 = value;
			u32(u.u32);
		}

		inline void u64(uint64_t value)
		{
			u32(value & 0xffffffffu);
			u32(value >> 32);
		}

		template <typename T>
		inline void pointer(T* ptr)
		{
			u64(reinterpret_cast<uintptr_t>(ptr));
		}

		inline void string(const char* str)
		{
			char c;
			u32(0xff);
			while ((c = *str++) != '\0')
				u32(uint8_t(c));
		}

		inline void string(const std::string& str)
		{
			u32(0xff);
			for (auto& c : str)
				u32(uint8_t(c));
		}

		inline Hash get() const
		{
			return h;
		}

	private:
		Hash h = 0xcbf29ce484222325ull;
	};
}
