#include "fence.hpp"
#include "device.hpp"

namespace Vulkan
{
	FenceHolder::~FenceHolder()
	{
		if (fence != VK_NULL_HANDLE)
		{
			if (internal_sync)
				device->ResetFenceNolock(fence, observed_wait);
			else
				device->ResetFence(fence, observed_wait);
		}
	}

	VkFence FenceHolder::GetFence() const
	{
		return fence;
	}

	void FenceHolder::Wait()
	{
		auto& table = device->GetDeviceTable();

#ifdef QM_VULKAN_MT
		// Waiting for the same VkFence in parallel is not allowed, and there seems to be some shenanigans on Intel
		// when waiting for a timeline semaphore in parallel with same value as well.
		std::lock_guard<std::mutex> holder{ lock };
#endif
		if (observed_wait)
			return;

		if (timeline_value != 0)
		{
			VK_ASSERT(timeline_semaphore);
			VkSemaphoreWaitInfoKHR info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR };
			info.semaphoreCount = 1;
			info.pSemaphores = &timeline_semaphore;
			info.pValues = &timeline_value;
			if (table.vkWaitSemaphoresKHR(device->GetDevice(), &info, UINT64_MAX) != VK_SUCCESS)
				QM_LOG_ERROR("Failed to Wait for timeline semaphore!\n");
			else
				observed_wait = true;
		}
		else
		{
			if (table.vkWaitForFences(device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
				QM_LOG_ERROR("Failed to Wait for fence!\n");
			else
				observed_wait = true;
		}
	}

	bool FenceHolder::WaitTimeout(uint64_t timeout)
	{
		bool ret = false;
		auto& table = device->GetDeviceTable();
		if (timeline_value != 0)
		{
			VK_ASSERT(timeline_semaphore);
			VkSemaphoreWaitInfoKHR info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR };
			info.semaphoreCount = 1;
			info.pSemaphores = &timeline_semaphore;
			info.pValues = &timeline_value;
			ret = table.vkWaitSemaphoresKHR(device->GetDevice(), &info, timeout) == VK_SUCCESS;
		}
		else
			ret = table.vkWaitForFences(device->GetDevice(), 1, &fence, VK_TRUE, timeout) == VK_SUCCESS;

		if (ret)
			observed_wait = true;
		return ret;
	}

	void FenceHolderDeleter::operator()(Vulkan::FenceHolder* fence)
	{
		fence->device->handle_pool.fences.free(fence);
	}
}
