#pragma once

#include <algorithm>

namespace Util
{
	template <typename T, size_t N>
	class StackAllocator
	{
	public:
		//Allocates count objects from the stack and returns a pointer to them
		T* allocate(size_t count)
		{
			if (count == 0)
				return nullptr;
			if (offset + count > N)
				return nullptr;

			T* ret = buffer + offset;
			offset += count;
			return ret;
		}

		//Allocates count objects and clears then to the default value before returning a pointer to them
		T* allocate_cleared(size_t count)
		{
			T* ret = allocate(count);
			if (ret)
				std::fill(ret, ret + count, T());
			return ret;
		}

		//Resets the stack
		void reset()
		{
			offset = 0;
		}

	private:
		T buffer[N];
		size_t offset = 0;
	};
}