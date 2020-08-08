#include "buffer.hpp"
#include "device.hpp"

namespace Vulkan
{
    Buffer::Buffer(Device* device_, VkBuffer buffer_, const DeviceAllocation& alloc_, const BufferCreateInfo& info_)
        : Cookie(device_)
        , device(device_)
        , buffer(buffer_)
        , alloc(alloc_)
        , info(info_)
    {
    }

	Buffer::~Buffer()
	{
		if (internal_sync)
			device->DestroyBufferNolock(buffer, alloc);
		else
			device->DestroyBuffer(buffer, alloc);
	}

	void BufferDeleter::operator()(Buffer* buffer)
	{
		buffer->device->handle_pool.buffers.free(buffer);
	}

	BufferView::BufferView(Device* device_, VkBufferView view_, const BufferViewCreateInfo& create_info_)
		: Cookie(device_)
		, device(device_)
		, view(view_)
		, info(create_info_)
	{
	}

	BufferView::~BufferView()
	{
		if (view != VK_NULL_HANDLE)
		{
			if (internal_sync)
				device->DestroyBufferViewNolock(view);
			else
				device->DestroyBufferView(view);
		}
	}

	void BufferViewDeleter::operator()(BufferView* view)
	{
		view->device->handle_pool.buffer_views.free(view);
	}

}
