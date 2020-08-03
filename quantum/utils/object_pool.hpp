#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>
#include <stdlib.h>
#include "aligned_alloc.hpp"

namespace Util
{
	//Represents an object pool. All memory allocated by pool will be retained until it is destroyed.
	template<typename T>
	class ObjectPool
	{
	public:
		template<typename... P>
		T* allocate(P&&... p)
		{
			//If vacants empty, fill it.
			if (vacants.empty())
			{
				//Exponentially increse ammount of memory allocated
				unsigned num_objects = 64u << memory.size();

				size_t alignment = size_t(64) > alignof(T) ? size_t(64) : alignof(T);
				T* ptr = static_cast<T*>(memalign_alloc(alignment, num_objects * sizeof(T)));
				if (!ptr)
					return nullptr;

				for (unsigned i = 0; i < num_objects; i++)
					vacants.push_back(&ptr[i]);

				memory.emplace_back(ptr);
			}

			T* ptr = vacants.back();
			vacants.pop_back();
			new(ptr) T(std::forward<P>(p)...);
			return ptr;
		}

		void free(T* ptr)
		{
			ptr->~T();
			vacants.push_back(ptr);
		}

		void clear()
		{
			vacants.clear();
			memory.clear();
		}

	protected:
		std::vector<T*> vacants;

		//Deleter for aligned memory
		struct MallocDeleter
		{
			void operator()(T* ptr)
			{
				memalign_free(ptr);
			}
		};

		std::vector<std::unique_ptr<T, MallocDeleter>> memory;
	};

	template<typename T>
	class ThreadSafeObjectPool : private ObjectPool<T>
	{
	public:
		template<typename... P>
		T* allocate(P&&... p)
		{
			std::lock_guard<std::mutex> holder{ lock };
			return ObjectPool<T>::allocate(std::forward<P>(p)...);
		}

		void free(T* ptr)
		{
			ptr->~T();
			std::lock_guard<std::mutex> holder{ lock };
			this->vacants.push_back(ptr);
		}

		void clear()
		{
			std::lock_guard<std::mutex> holder{ lock };
			ObjectPool<T>::clear();
		}

	private:
		std::mutex lock;
	};
}
