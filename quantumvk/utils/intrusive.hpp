#pragma once

#include <cstddef>
#include <utility>
#include <memory>
#include <atomic>
#include <type_traits>

namespace Util
{
	class SingleThreadCounter
	{
	public:
		inline void AddRef()
		{
			count++;
		}

		inline size_t GetRefCount()
		{
			return count;
		}

		inline bool Release()
		{
			return --count == 0;
		}

	private:
		size_t count = 0;
	};

	class MultiThreadCounter
	{
	public:
		MultiThreadCounter()
		{
			count.store(0, std::memory_order_relaxed);
		}

		inline void AddRef()
		{
			count.fetch_add(1, std::memory_order_relaxed);
		}

		inline size_t GetRefCount()
		{
			return count.load(std::memory_order_relaxed);
		}

		inline bool Release()
		{
			auto result = count.fetch_sub(1, std::memory_order_acq_rel);
			return result == 1;
		}

	private:
		std::atomic_size_t count;
	};

	template <typename T>
	class IntrusivePtr;

	//Enables an object to be RefCounted
	template <typename T, typename Deleter = std::default_delete<T>, typename ReferenceOps = SingleThreadCounter>
	class IntrusivePtrEnabled
	{
	public:
		using IntrusivePtrType = IntrusivePtr<T>;
		using EnabledBase = T;
		using EnabledDeleter = Deleter;
		using EnabledReferenceOp = ReferenceOps;

		void ReleaseReference()
		{
			if (reference_count.Release())
				Deleter()(static_cast<T*>(this));
		}

		size_t GetRefCount()
		{
			return reference_count.GetRefCount();
		}

		void AddRef()
		{
			reference_count.AddRef();
		}

		IntrusivePtrEnabled() = default;

		IntrusivePtrEnabled(const IntrusivePtrEnabled&) = delete;

		void operator=(const IntrusivePtrEnabled&) = delete;

	protected:
		Util::IntrusivePtr<T> ReferenceFromThis();

	private:
		ReferenceOps reference_count;
	};

	//Intrusitve shared pointer
	template <typename T>
	class IntrusivePtr
	{
	public:
		template <typename U>
		friend class IntrusivePtr;

		IntrusivePtr() = default;

		explicit IntrusivePtr(T* handle)
			: data(handle)
		{
			if(data)
				data->AddRef();
		}

		T& operator*()
		{
			return *data;
		}

		const T& operator*() const
		{
			return *data;
		}

		T* operator->()
		{
			return data;
		}

		const T* operator->() const
		{
			return data;
		}

		operator bool() { return data != nullptr; }
		operator bool() const { return data != nullptr; }


		bool operator==(const IntrusivePtr& other) const
		{
			return data == other.data;
		}

		bool operator!=(const IntrusivePtr& other) const
		{
			return data != other.data;
		}

		T* Get()
		{
			return data;
		}

		const T* Get() const
		{
			return data;
		}

		void Reset()
		{
			using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

			// Static up-cast here to avoid potential issues with multiple intrusive inheritance.
			// Also makes sure that the pointer type actually inherits from this type.
			if (data)
				static_cast<ReferenceBase*>(data)->ReleaseReference();
			data = nullptr;
		}

		template <typename U>
		IntrusivePtr& operator=(const IntrusivePtr<U>& other)
		{
			static_assert(std::is_base_of<T, U>::value,
				"Cannot safely assign downcasted intrusive pointers.");

			using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

			Reset();
			data = static_cast<T*>(other.data);

			// Static up-cast here to avoid potential issues with multiple intrusive inheritance.
			// Also makes sure that the pointer type actually inherits from this type.
			if (data)
				static_cast<ReferenceBase*>(data)->AddRef();
			return *this;
		}

		IntrusivePtr& operator=(const IntrusivePtr& other)
		{
			using ReferenceBase = IntrusivePtrEnabled<
				typename T::EnabledBase,
				typename T::EnabledDeleter,
				typename T::EnabledReferenceOp>;

			if (this != &other)
			{
				Reset();
				data = other.data;
				if (data)
					static_cast<ReferenceBase*>(data)->AddRef();
			}
			return *this;
		}

		template <typename U>
		IntrusivePtr(const IntrusivePtr<U>& other)
		{
			*this = other;
		}

		IntrusivePtr(const IntrusivePtr& other)
		{
			*this = other;
		}

		~IntrusivePtr()
		{
			Reset();
		}

		template <typename U>
		IntrusivePtr& operator=(IntrusivePtr<U>&& other) noexcept
		{
			Reset();
			data = other.data;
			other.data = nullptr;
			return *this;
		}

		IntrusivePtr& operator=(IntrusivePtr&& other) noexcept
		{
			if (this != &other)
			{
				Reset();
				data = other.data;
				other.data = nullptr;
			}
			return *this;
		}

		template <typename U>
		IntrusivePtr(IntrusivePtr<U>&& other) noexcept
		{
			*this = std::move(other);
		}

		template <typename U>
		IntrusivePtr(IntrusivePtr&& other) noexcept
		{
			*this = std::move(other);
		}

	private:
		T* data = nullptr;
	};

	template <typename T, typename Deleter, typename ReferenceOps>
	IntrusivePtr<T> IntrusivePtrEnabled<T, Deleter, ReferenceOps>::ReferenceFromThis()
	{
		AddRef();
		return IntrusivePtr<T>(static_cast<T*>(this));
	}

	template <typename Derived>
	using DerivedIntrusivePtrType = IntrusivePtr<Derived>;

	template <typename T, typename... P>
	DerivedIntrusivePtrType<T> MakeHandle(P&&... p)
	{
		return DerivedIntrusivePtrType<T>(new T(std::forward<P>(p)...));
	}

	template <typename Base, typename Derived, typename... P>
	typename Base::IntrusivePtrType MakeDerivedHandle(P&&... p)
	{
		return typename Base::IntrusivePtrType(new Derived(std::forward<P>(p)...));
	}

	template <typename T>
	using ThreadSafeIntrusivePtrEnabled = IntrusivePtrEnabled<T, std::default_delete<T>, MultiThreadCounter>;
}
