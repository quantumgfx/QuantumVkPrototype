#pragma once

#include "vulkan_headers.hpp"
#include "vulkan_common.hpp"
#include "cookie.hpp"
#include "utils/object_pool.hpp"

namespace Vulkan
{
	class Device;
	class EventHolder;

	struct EventHolderDeleter
	{
		void operator()(EventHolder* event);
	};

	//Ref-counted wrapper class for vkevent
	class EventHolder : public Util::IntrusivePtrEnabled<EventHolder, EventHolderDeleter, HandleCounter>,
		public InternalSyncEnabled
	{
	public:
		friend struct EventHolderDeleter;

		~EventHolder();

		const VkEvent& get_event() const
		{
			return event;
		}

		VkPipelineStageFlags get_stages() const
		{
			return stages;
		}

		void set_stages(VkPipelineStageFlags stages_)
		{
			stages = stages_;
		}

	private:
		friend class Util::ObjectPool<EventHolder>;
		EventHolder(Device* device_, VkEvent event_)
			: device(device_)
			, event(event_)
		{
		}

		Device* device;
		VkEvent event;
		VkPipelineStageFlags stages = 0;
	};

	using PipelineEvent = Util::IntrusivePtr<EventHolder>;

}
