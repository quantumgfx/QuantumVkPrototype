#pragma once

#include "intrusive.hpp"

#include <cstdint>

namespace Util
{

	//Represents data on the heap, contains a size and size bytes of data which are cleaned up when this goes out of scope
	template <typename ReferenceOps>
	class HeapData : public IntrusivePtrEnabled<HeapData<ReferenceOps>, std::default_delete<HeapData<ReferenceOps>>, ReferenceOps>
	{
	public:

		HeapData(void* inital_data, size_t initial_size)
			: size(initial_size)
		{
			data = new uint8_t[size];
			memcpy(data, inital_data, initial_size);
		}

		~HeapData()
		{
			delete[] data;
		}

		uint8_t* GetData()
		{
			return data;
		}

		size_t GetSize()
		{
			return size;
		}

	private:

		uint8_t* data;
		size_t size;

	};

	//Ref counted heap data. As long as the handle is retained, memory will remain valid (useful for passing memory allocated with new out of a function)
	template<typename ReferenceOps>
	using RetainedHeapData = IntrusivePtr<HeapData<ReferenceOps>>;

	template<typename ReferenceOps>
	RetainedHeapData<ReferenceOps> CreateRetainedHeapData(void* inital_data, size_t initial_size)
	{
		return RetainedHeapData<ReferenceOps>(new HeapData<ReferenceOps>(inital_data, initial_size));
	}

}