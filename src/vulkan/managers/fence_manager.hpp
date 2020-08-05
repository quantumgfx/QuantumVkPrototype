#pragma once

#include "vulkan_headers.hpp"
#include <vector>

namespace Vulkan
{
	class Device;
	//Class that manages use and reuse of fences
	class FenceManager
	{
	public:
		void init(Device* device);
		~FenceManager();

		VkFence request_cleared_fence();
		void recycle_fence(VkFence fence);

	private:
		Device* device = nullptr;
		const VolkDeviceTable* table = nullptr;
		std::vector<VkFence> fences;
	};
}
