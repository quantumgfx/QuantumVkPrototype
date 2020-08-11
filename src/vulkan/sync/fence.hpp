#pragma once

#include "vulkan/vulkan_headers.hpp"
#include "vulkan/vulkan_common.hpp"

#include "utils/object_pool.hpp"
#include "vulkan/misc/cookie.hpp"
#ifdef QM_VULKAN_MT
	#include <mutex>
#endif

namespace Vulkan
{
	class Device;

	class FenceHolder;
	struct FenceHolderDeleter
	{
		void operator()(FenceHolder* fence);
	};

	// If timeline semaphores are supported, fence holders use that internally. Otherwise normal VkFences are used
	class FenceHolder : public Util::IntrusivePtrEnabled<FenceHolder, FenceHolderDeleter, HandleCounter>, public InternalSyncEnabled
	{
	public:
		friend struct FenceHolderDeleter;
		friend class WSI;

		~FenceHolder();
		void Wait();
		bool WaitTimeout(uint64_t nsec);

	private:
		friend class Util::ObjectPool<FenceHolder>;
		FenceHolder(Device* device_, VkFence fence_)
			: device(device_),
			fence(fence_),
			timeline_semaphore(VK_NULL_HANDLE),
			timeline_value(0)
		{
		}

		FenceHolder(Device* device_, uint64_t value, VkSemaphore timeline_semaphore_)
			: device(device_),
			fence(VK_NULL_HANDLE),
			timeline_semaphore(timeline_semaphore_),
			timeline_value(value)
		{
			//VK_ASSERT(value > 0);
		}

		VkFence GetFence() const;

		Device* device;
		VkFence fence;
		VkSemaphore timeline_semaphore;
		uint64_t timeline_value;
		bool observed_wait = false;
#ifdef QM_VULKAN_MT
		std::mutex lock;
#endif
	};

	using Fence = Util::IntrusivePtr<FenceHolder>;
}
