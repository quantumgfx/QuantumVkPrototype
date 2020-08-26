#pragma once

#include "quantumvk/vulkan/vulkan_common.hpp"
#include "quantumvk/vulkan/vulkan_headers.hpp"
#include "quantumvk/vulkan/misc/cookie.hpp"
#include "quantumvk/utils/object_pool.hpp"

namespace Vulkan
{
	class Device;

	class SemaphoreHolder;
	struct SemaphoreHolderDeleter
	{
		void operator()(SemaphoreHolder* semaphore);
	};

	//Ref-counted wrapper class for vksemaphore
	class SemaphoreHolder : public Util::IntrusivePtrEnabled<SemaphoreHolder, SemaphoreHolderDeleter, HandleCounter>, public InternalSyncEnabled
	{
	public:
		friend struct SemaphoreHolderDeleter;

		~SemaphoreHolder();

		// Returns the VkSemaphore this wrapper holders
		const VkSemaphore& GetSemaphore() const
		{
			return semaphore;
		}

		bool IsSignalled() const
		{
			return signalled;
		}

		uint64_t GetTimelineValue() const
		{
			return timeline;
		}

		// Retrives the vksemaphore to be used in a wait operation (a semaphore can 
		// typically be used in two operations, a signal operation and a wait operation)
		VkSemaphore Consume()
		{
			auto ret = semaphore;
			VK_ASSERT(semaphore);
			VK_ASSERT(signalled);
			semaphore = VK_NULL_HANDLE;
			signalled = false;
			return ret;
		}

		VkSemaphore ReleaseSemaphore()
		{
			auto ret = semaphore;
			semaphore = VK_NULL_HANDLE;
			signalled = false;
			return ret;
		}

		bool CanRecycle() const
		{
			return !should_destroy_on_consume;
		}

		void WaitExternal()
		{
			VK_ASSERT(semaphore);
			VK_ASSERT(signalled);
			signalled = false;
		}

		void SignalExternal()
		{
			VK_ASSERT(!signalled);
			VK_ASSERT(semaphore);
			signalled = true;
		}

		void DestroyOnConsume()
		{
			should_destroy_on_consume = true;
		}

		void SignalPendingWaits()
		{
			pending = true;
		}

		bool IsPendingWait() const
		{
			return pending;
		}

	private:
		friend class Util::ObjectPool<SemaphoreHolder>;
		SemaphoreHolder(Device* device_, VkSemaphore semaphore_, bool signalled_)
			: device(device_)
			, semaphore(semaphore_)
			, timeline(0)
			, signalled(signalled_)
		{
		}

		SemaphoreHolder(Device* device_, uint64_t timeline_, VkSemaphore semaphore_)
			: device(device_), semaphore(semaphore_), timeline(timeline_)
		{
			VK_ASSERT(timeline > 0);
		}

		Device* device;
		VkSemaphore semaphore;
		uint64_t timeline;
		bool signalled = true;
		bool pending = false;
		bool should_destroy_on_consume = false;
	};

	using Semaphore = Util::IntrusivePtr<SemaphoreHolder>;
}
