#pragma once

#include <vector>
#include <mutex>

namespace Util
{

	template<typename>
	class RetainedDynamicArray;

	class TypelessRetainedAlloc
	{
	public:
		void* ptr;
		size_t capacity;
	};

	// Vulkan uses a lot of arrays passed into functions as arguments. This struct minimizes allocations by host for variable sized arrays.
	class DynamicArrayPool
	{

	public:

		DynamicArrayPool()
		{
		}

		~DynamicArrayPool()
		{
			for (auto& alloc : allocs)
				delete[] (uint8_t*)alloc.ptr;
		}

		template<typename T>
		RetainedDynamicArray<T> RetainedAllocateArray(size_t count);

		template<typename T>
		void RetainedFreeArray(RetainedDynamicArray<T> array);

	private:

		std::vector<TypelessRetainedAlloc> allocs;

	};

	template<typename T>
	class RetainedDynamicArray
	{
	public:

		RetainedDynamicArray(const TypelessRetainedAlloc& heap_alloc)
			: alloc(heap_alloc)
		{
			ptr = static_cast<T*>(heap_alloc.ptr);
			count = heap_alloc.capacity / sizeof(T);
		}

		T* Data() const { return ptr; }
		size_t MaxElements() const { return count; }

		T& operator[](size_t index)
		{
			return ptr[index];
		}

	private:
		T* ptr;
		size_t count;

		TypelessRetainedAlloc alloc;

		friend class DynamicArrayPool;
	};

	template<typename T>
	RetainedDynamicArray<T> DynamicArrayPool::RetainedAllocateArray(size_t count)
	{
		size_t required_size = count * sizeof(T);
		// First check if arrays is empty (ie all allocated arrays are in use). If so allocate additional array.
		if (allocs.empty())
		{
			TypelessRetainedAlloc new_array;
			new_array.capacity = required_size;
			new_array.ptr = new uint8_t[required_size];
			return new_array;
		}

		// Check if another array is big enough

		int index = -1;
		for (unsigned i = 0; i < allocs.size(); i++)
		{
			if (allocs[i].capacity >= required_size)
			{
				index = i;
				break;
			}
		}

		if (index != -1)
		{
			TypelessRetainedAlloc alloc = allocs[index];
			allocs.erase(allocs.begin() + index);
			return alloc;
		}

		// Reallocate array to fit new size
		TypelessRetainedAlloc realloc = allocs.back();
		allocs.pop_back();

		delete[](uint8_t*)realloc.ptr;
		realloc.ptr = new uint8_t[required_size];
		realloc.capacity = required_size;

		return realloc;
	}

	template<typename T>
	void DynamicArrayPool::RetainedFreeArray(RetainedDynamicArray<T> array)
	{
		allocs.push_back(array.alloc);
	}

	class ThreadSafeDynamicArrayPool : DynamicArrayPool
	{

	public:

		template<typename T>
		RetainedDynamicArray<T> RetainedAllocateArray(size_t count)
		{
			std::lock_guard lock(m_Mutex);
			return DynamicArrayPool::RetainedAllocateArray<T>(count);
		}

		template<typename T>
		void RetainedFreeArray(RetainedDynamicArray<T> array)
		{
			std::lock_guard lock(m_Mutex);
			DynamicArrayPool::RetainedFreeArray(array);
		}


	private:

		std::mutex m_Mutex;

	};


}