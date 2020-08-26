#include "semaphore.hpp"
#include "quantumvk/vulkan/device.hpp"

namespace Vulkan
{
	SemaphoreHolder::~SemaphoreHolder()
	{
		if (timeline == 0 && semaphore)
		{
			if (internal_sync)
			{
				if (IsSignalled())
					device->DestroySemaphoreNolock(semaphore);
				else
					device->RecycleSemaphoreNolock(semaphore);
			}
			else
			{
				if (IsSignalled())
					device->DestroySemaphore(semaphore);
				else
					device->RecycleSemaphore(semaphore);
			}
		}
	}

	void SemaphoreHolderDeleter::operator()(Vulkan::SemaphoreHolder* semaphore)
	{
		semaphore->device->handle_pool.semaphores.free(semaphore);
	}
}
