#pragma once

#include "object_pool.hpp"

#include "logging.hpp"

namespace Util
{

	class IntrusiveObjectPoolEnabled
	{
	public:

		uint64_t GetIndex()
		{
			return index;
		}

		void SetIndex(uint64_t index_)
		{
			index = index_;
		}

	private:

		uint64_t index;
	};


	// An object pool that tracks it's current allocations. i.e. it allows you to loop through every object currently allocated by the pool.
	template<typename T>
	class IntrusiveObjectPool
	{

	public:

		~IntrusiveObjectPool()
		{

			if(!objects.empty())
				QM_LOG_ERROR("Some objects still in use at time of object pool destruction\n");
			for (T* object : objects)
			{
				if (object)
					object->~T();
			}
		}

		template<typename... P>
		T* allocate(P&&... p)
		{
			T* ptr = pool.allocate(std::forward<P>(p)...);
			objects.push_back(ptr);
			
			if (!freed.empty())
			{
				ptr->SetIndex(freed.back());
				freed.pop_back();
			}
			else
			{
				ptr->SetIndex(current_index++);
			}
			return ptr;
		}

		void free(T* ptr)
		{
			objects[ptr->GetIndex()] = nullptr;
			freed.push_back(ptr->GetIndex());
			pool.free(ptr);
		}

		// Loops through every valid object in the pool and calls the function on it
		void for_each(void(*func)(T*))
		{
			for (T* object : objects)
			{
				if(object)
					func(object);
			}
		}


	private:

		ObjectPool<T> pool;

		uint64_t current_index = 0;
		std::vector<T*> objects;
		std::vector<uint64_t> freed;

	};

	template<typename T>
	class ThreadSafeIntrusiveObjectPool : private IntrusiveObjectPool<T>
	{
	public:
		template<typename... P>
		T* allocate(P&&... p)
		{
			std::lock_guard<std::mutex> holder{ lock };
			return IntrusiveObjectPool<T>::allocate(std::forward<P>(p)...);
		}

		void free(T* ptr)
		{
			std::lock_guard<std::mutex> holder{ lock };
			IntrusiveObjectPool<T>::free(ptr);
		}

		void for_each(void(*func)(T*))
		{
			std::lock_guard<std::mutex> holder{ lock };
			IntrusiveObjectPool<T>::for_each(func);
		}

	private:
		std::mutex lock;
	};

}