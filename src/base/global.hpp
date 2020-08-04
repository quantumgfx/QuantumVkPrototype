#pragma once

#include <extern/volk/volk.h>

#include <vector>

namespace Base 
{
	struct Global 
	{

		VkPhysicalDevice(*choose_gpu_func)(std::vector<VkPhysicalDevice>&);
		bool force_no_validation;
		bool force_timeline_semaphore;

		Global()
		{
			choose_gpu_func = [](std::vector<VkPhysicalDevice>& gpus) 
			{
				return gpus.front();
			};

			force_no_validation = false;
			force_timeline_semaphore = false;
		}
	};

	extern Global global;
}