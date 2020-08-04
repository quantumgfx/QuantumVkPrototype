#pragma once

namespace Util
{
	template <typename T>
	struct IntrusiveListEnabled
	{
		IntrusiveListEnabled<T>* prev = nullptr;
		IntrusiveListEnabled<T>* next = nullptr;
	};
	
	//Essentially just std list, but one which stores the prev and next pointers inside the object itself.
	//This makes it faster and more optimized.
	template <typename T>
	class IntrusiveList
	{
	public:
		void clear()
		{
			head = nullptr;
			tail = nullptr;
		}

		class Iterator
		{
		public:
			friend class IntrusiveList<T>;
			Iterator(IntrusiveListEnabled<T>* node_)
				: node(node_)
			{
			}

			Iterator() = default;

			explicit operator bool() const
			{
				return node != nullptr;
			}

			bool operator==(const Iterator& other) const
			{
				return node == other.node;
			}

			bool operator!=(const Iterator& other) const
			{
				return node != other.node;
			}

			T& operator*()
			{
				return *static_cast<T*>(node);
			}

			const T& operator*() const
			{
				return *static_cast<T*>(node);
			}

			T* get()
			{
				return static_cast<T*>(node);
			}

			const T* get() const
			{
				return static_cast<const T*>(node);
			}

			T* operator->()
			{
				return static_cast<T*>(node);
			}

			const T* operator->() const
			{
				return static_cast<T*>(node);
			}

			Iterator& operator++()
			{
				node = node->next;
				return *this;
			}

			Iterator& operator--()
			{
				node = node->prev;
				return *this;
			}

		private:
			IntrusiveListEnabled<T>* node = nullptr;
		};

		Iterator begin()
		{
			return Iterator(head);
		}

		Iterator rbegin()
		{
			return Iterator(tail);
		}

		Iterator end()
		{
			return Iterator();
		}

		Iterator erase(Iterator itr)
		{
			auto* node = itr.get();
			auto* next = node->next;
			auto* prev = node->prev;

			if (prev)
				prev->next = next;
			else
				head = next;

			if (next)
				next->prev = prev;
			else
				tail = prev;

			return next;
		}

		void insert_front(Iterator itr)
		{
			auto* node = itr.get();
			if (head)
				head->prev = node;
			else
				tail = node;

			node->next = head;
			node->prev = nullptr;
			head = node;
		}

		void insert_back(Iterator itr)
		{
			auto* node = itr.get();
			if (tail)
				tail->next = node;
			else
				head = node;

			node->prev = tail;
			node->next = nullptr;
			tail = node;
		}

		void move_to_front(IntrusiveList<T>& other, Iterator itr)
		{
			other.erase(itr);
			insert_front(itr);
		}

		void move_to_back(IntrusiveList<T>& other, Iterator itr)
		{
			other.erase(itr);
			insert_back(itr);
		}

		bool empty() const
		{
			return head == nullptr;
		}

	private:
		IntrusiveListEnabled<T>* head = nullptr;
		IntrusiveListEnabled<T>* tail = nullptr;
	};
}