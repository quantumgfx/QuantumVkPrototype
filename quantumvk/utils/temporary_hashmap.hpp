#pragma once

#include "hash.hpp"
#include "object_pool.hpp"
#include "intrusive_list.hpp"
#include "intrusive_hash_map.hpp"
#include <vector>

namespace Util
{
	/**
	 * @brief Object must inheiret from this class to be used in the TemporaryHashmap
	 * @tparam T Object Type
	*/
	template <typename T>
	class TemporaryHashmapEnabled
	{
	public:
		void set_hash(Hash hash_)
		{
			hash = hash_;
		}

		void set_index(uint32_t index_)
		{
			index = index_;
		}

		Hash get_hash()
		{
			return hash;
		}

		uint32_t get_index() const
		{
			return index;
		}

	private:
		Hash hash = 0;
		uint32_t index = 0;
	};

	
	template <typename T, uint32_t RingSize = 4, bool ReuseObjects = false>
	class TemporaryHashmap
	{
	public:
		~TemporaryHashmap()
		{
			clear();
		}

		/**
		 * @brief Clears all elements in the temporary hash map, and frees and destructs any vacant objects
		*/
		void clear()
		{
			for (auto& ring : rings)
			{
				for (auto& node : ring)
					object_pool.free(static_cast<T*>(&node));
				ring.clear();
			}

			hashmap.clear();

			for (auto& vacant : vacants)
				object_pool.free(static_cast<T*>(&*vacant));
			vacants.clear();
			object_pool.clear();
		}

		/**
		 * @brief Begins a new frame, if Reuse objects is true then it moves every object that has existed for RingSize frames into
		 * the vacants list. Otherwise it destructs all objects that have existed for more that ringsize frames.
		*/
		void begin_frame()
		{
			index = (index + 1) & (RingSize - 1);
			for (auto& node : rings[index])
			{
				hashmap.erase(node.get_hash());
				free_object(&node, ReuseTag<ReuseObjects>());
			}
			rings[index].clear();
		}

		T* request(Hash hash)
		{
			auto* v = hashmap.find(hash);
			if (v)
			{
				auto node = v->get();
				if (node->get_index() != index)
				{
					rings[index].move_to_front(rings[node->get_index()], node);
					node->set_index(index);
				}

				return &*node;
			}
			else
				return nullptr;
		}

		template <typename... P>
		void make_vacant(P&&... p)
		{
			vacants.push_back(object_pool.allocate(std::forward<P>(p)...));
		}

		T* request_vacant(Hash hash)
		{
			if (vacants.empty())
				return nullptr;

			auto top = vacants.back();
			vacants.pop_back();
			top->set_index(index);
			top->set_hash(hash);
			hashmap.emplace_replace(hash, top);
			rings[index].insert_front(top);
			return &*top;
		}

		template <typename... P>
		T* emplace(Hash hash, P&&... p)
		{
			auto* node = object_pool.allocate(std::forward<P>(p)...);
			node->set_index(index);
			node->set_hash(hash);
			hashmap.emplace_replace(hash, node);
			rings[index].insert_front(node);
			return node;
		}

	private:
		IntrusiveList<T> rings[RingSize];
		ObjectPool<T> object_pool;
		uint32_t index = 0;
		IntrusiveHashMap<IntrusivePODWrapper<typename IntrusiveList<T>::Iterator>> hashmap;
		std::vector<typename IntrusiveList<T>::Iterator> vacants;

		template <bool Reuse>
		struct ReuseTag
		{
		};

		void free_object(T* object, const ReuseTag<false>&)
		{
			object_pool.free(object);
		}

		void free_object(T* object, const ReuseTag<true>&)
		{
			vacants.push_back(object);
		}
	};
}
