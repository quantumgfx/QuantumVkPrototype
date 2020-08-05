#pragma once

#include "vulkan_headers.hpp"
#include <vector>

namespace Vulkan
{
	class Device;
	//Class that manages use and reuse of semaphores
	class SemaphoreManager
	{
	public:
		void init(Device* device);
		~SemaphoreManager();

		VkSemaphore request_cleared_semaphore();
		void recycle(VkSemaphore semaphore);

	private:
		Device* device = nullptr;
		const VolkDeviceTable* table = nullptr;
		std::vector<VkSemaphore> semaphores;
	};
}
