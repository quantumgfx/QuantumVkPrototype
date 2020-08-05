#pragma once

#include "vulkan_common.hpp"
#include "vulkan_headers.hpp"
#include "cookie.hpp"
#include "utils/object_pool.hpp"

namespace Vulkan
{
	class Device;

	class SemaphoreHolder;
	struct SemaphoreHolderDeleter
	{
		void operator()(SemaphoreHolder* semaphore);
	};
	//Ref-counted wrapper class for vksampler
	class SemaphoreHolder : public Util::IntrusivePtrEnabled<SemaphoreHolder, SemaphoreHolderDeleter, HandleCounter>,
		public InternalSyncEnabled
	{
	public:
		friend struct SemaphoreHolderDeleter;

		~SemaphoreHolder();

		const VkSemaphore& get_semaphore() const
		{
			return semaphore;
		}

		bool is_signalled() const
		{
			return signalled;
		}

		uint64_t get_timeline_value() const
		{
			return timeline;
		}

		VkSemaphore consume()
		{
			auto ret = semaphore;
			VK_ASSERT(semaphore);
			VK_ASSERT(signalled);
			semaphore = VK_NULL_HANDLE;
			signalled = false;
			return ret;
		}

		VkSemaphore release_semaphore()
		{
			auto ret = semaphore;
			semaphore = VK_NULL_HANDLE;
			signalled = false;
			return ret;
		}

		bool can_recycle() const
		{
			return !should_destroy_on_consume;
		}

		void wait_external()
		{
			VK_ASSERT(semaphore);
			VK_ASSERT(signalled);
			signalled = false;
		}

		void signal_external()
		{
			VK_ASSERT(!signalled);
			VK_ASSERT(semaphore);
			signalled = true;
		}

		void destroy_on_consume()
		{
			should_destroy_on_consume = true;
		}

		void signal_pending_wait()
		{
			pending = true;
		}

		bool is_pending_wait() const
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
