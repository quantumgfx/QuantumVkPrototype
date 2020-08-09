#include "fence_manager.hpp"
#include "vulkan/device.hpp"

namespace Vulkan
{
	void FenceManager::Init(Device* device_)
	{
		device = device_;
		table = &device->GetDeviceTable();
	}

	VkFence FenceManager::RequestClearedFence()
	{
		if (!fences.empty())
		{
			auto ret = fences.back();
			fences.pop_back();
			return ret;
		}
		else
		{
			VkFence fence;
			VkFenceCreateInfo info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
			table->vkCreateFence(device->GetDevice(), &info, nullptr, &fence);
			return fence;
		}
	}

	void FenceManager::RecycleFence(VkFence fence)
	{
		fences.push_back(fence);
	}

	FenceManager::~FenceManager()
	{
		for (auto& fence : fences)
			table->vkDestroyFence(device->GetDevice(), fence, nullptr);
	}
}
