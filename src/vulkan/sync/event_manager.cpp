#include "event_manager.hpp"
#include "vulkan/device.hpp"

namespace Vulkan
{
	EventManager::~EventManager()
	{
		if (!workaround)
			for (auto& event : events)
				table->vkDestroyEvent(device->GetDevice(), event, nullptr);
	}

	void EventManager::RecycleEvent(VkEvent event)
	{
		if (!workaround && event != VK_NULL_HANDLE)
		{
			table->vkResetEvent(device->GetDevice(), event);
			events.push_back(event);
		}
	}

	VkEvent EventManager::RequestClearedEvent()
	{
		if (workaround)
		{
			// Can't use reinterpret_cast because of MSVC.
			return (VkEvent) ++workaround_counter;
		}
		else if (events.empty())
		{
			VkEvent event;
			VkEventCreateInfo info = { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO };
			table->vkCreateEvent(device->GetDevice(), &info, nullptr, &event);
			return event;
		}
		else
		{
			auto event = events.back();
			events.pop_back();
			return event;
		}
	}

	void EventManager::Init(Device* device_)
	{
		device = device_;
		table = &device->GetDeviceTable();
		workaround = device_->GetWorkarounds().emulate_event_as_pipeline_barrier;
	}
}