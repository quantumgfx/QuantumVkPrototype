#pragma once

#include "vulkan/vulkan_headers.hpp"
#include <vector>

namespace Vulkan
{
	class Device;
	//Identical to fence manager
	class EventManager
	{
	public:
		void Init(Device* device);
		~EventManager();

		VkEvent RequestClearedEvent();
		void RecycleEvent(VkEvent event);

	private:
		Device* device = nullptr;
		const VolkDeviceTable* table = nullptr;
		std::vector<VkEvent> events;
		uint64_t workaround_counter = 0;
		bool workaround = false;
	};
}