#include "pipeline_event.hpp"
#include "vulkan/device.hpp"

namespace Vulkan
{
	EventHolder::~EventHolder()
	{
		if (event)
		{
			if (internal_sync)
				device->DestroyEventNolock(event);
			else
				device->DestroyEvent(event);
		}
	}

	void EventHolderDeleter::operator()(Vulkan::EventHolder* event)
	{
		event->device->handle_pool.events.free(event);
	}
}