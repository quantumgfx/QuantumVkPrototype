#pragma once

#include "quantumvk/vulkan/vulkan_headers.hpp"
#include <vector>

namespace Vulkan
{
	class Device;
	// Class that manages use and reuse of semaphores
	// Identical to fence manager
	class SemaphoreManager
	{
	public:
		void Init(Device* device);
		~SemaphoreManager();

		VkSemaphore RequestClearedSemaphore();
		void RecycleSemaphore(VkSemaphore semaphore);

	private:
		Device* device = nullptr;
		const VolkDeviceTable* table = nullptr;
		std::vector<VkSemaphore> semaphores;
	};
}
