#include "semaphore_manager.hpp"
#include "quantumvk/vulkan/device.hpp"

namespace Vulkan
{
	void SemaphoreManager::Init(Device* device_)
	{
		device = device_;
		table = &device->GetDeviceTable();
	}

	SemaphoreManager::~SemaphoreManager()
	{
		for (auto& sem : semaphores)
			table->vkDestroySemaphore(device->GetDevice(), sem, nullptr);
	}

	void SemaphoreManager::RecycleSemaphore(VkSemaphore sem)
	{
		if (sem != VK_NULL_HANDLE)
			semaphores.push_back(sem);
	}

	VkSemaphore SemaphoreManager::RequestClearedSemaphore()
	{
		if (semaphores.empty())
		{
			VkSemaphore semaphore;
			VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			table->vkCreateSemaphore(device->GetDevice(), &info, nullptr, &semaphore);
			return semaphore;
		}
		else
		{
			auto sem = semaphores.back();
			semaphores.pop_back();
			return sem;
		}
	}
}