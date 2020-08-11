#include "command_pool.hpp"
#include "device.hpp"

namespace Vulkan
{
	CommandPool::CommandPool(Device* device_, uint32_t queue_family_index)
		: device(device_), table(&device_->GetDeviceTable())
	{
		VkCommandPoolCreateInfo info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		info.queueFamilyIndex = queue_family_index;
		table->vkCreateCommandPool(device->GetDevice(), &info, nullptr, &pool);
	}

	CommandPool::CommandPool(CommandPool&& other) noexcept
	{
		*this = std::move(other);
	}

	CommandPool& CommandPool::operator=(CommandPool&& other) noexcept
	{
		if (this != &other)
		{
			device = other.device;
			table = other.table;
			if (!buffers.empty())
				table->vkFreeCommandBuffers(device->GetDevice(), pool, buffers.size(), buffers.data());
			if (pool != VK_NULL_HANDLE)
				table->vkDestroyCommandPool(device->GetDevice(), pool, nullptr);

			pool = VK_NULL_HANDLE;
			buffers.clear();
			std::swap(pool, other.pool);
			std::swap(buffers, other.buffers);
			index = other.index;
			other.index = 0;
#ifdef VULKAN_DEBUG
			in_flight.clear();
			std::swap(in_flight, other.in_flight);
#endif
		}
		return *this;
	}

	CommandPool::~CommandPool()
	{
		if (!buffers.empty())
			table->vkFreeCommandBuffers(device->GetDevice(), pool, buffers.size(), buffers.data());
		if (!secondary_buffers.empty())
			table->vkFreeCommandBuffers(device->GetDevice(), pool, secondary_buffers.size(), secondary_buffers.data());
		if (pool != VK_NULL_HANDLE)
			table->vkDestroyCommandPool(device->GetDevice(), pool, nullptr);
	}

	void CommandPool::SignalSubmitted(VkCommandBuffer cmd)
	{
#ifdef VULKAN_DEBUG
		VK_ASSERT(in_flight.find(cmd) != end(in_flight));
		in_flight.erase(cmd);
#else
		(void)cmd;
#endif
	}

	VkCommandBuffer CommandPool::RequestSecondaryCommandBuffer()
	{
		if (secondary_index < secondary_buffers.size())
		{
			auto ret = secondary_buffers[secondary_index++];
#ifdef VULKAN_DEBUG
			VK_ASSERT(in_flight.find(ret) == std::end(in_flight));
			in_flight.insert(ret);
#endif
			return ret;
		}
		else
		{
			VkCommandBuffer cmd;
			VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			info.commandPool = pool;
			info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
			info.commandBufferCount = 1;

			table->vkAllocateCommandBuffers(device->GetDevice(), &info, &cmd);
#ifdef VULKAN_DEBUG
			VK_ASSERT(in_flight.find(cmd) == std::end(in_flight));
			in_flight.insert(cmd);
#endif
			secondary_buffers.push_back(cmd);
			secondary_index++;
			return cmd;
		}
	}

	VkCommandBuffer CommandPool::RequestCommandBuffer()
	{
		if (index < buffers.size())
		{
			auto ret = buffers[index++];
#ifdef VULKAN_DEBUG
			VK_ASSERT(in_flight.find(ret) == std::end(in_flight));
			in_flight.insert(ret);
#endif
			return ret;
		}
		else
		{
			VkCommandBuffer cmd;
			VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			info.commandPool = pool;
			info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			info.commandBufferCount = 1;

			table->vkAllocateCommandBuffers(device->GetDevice(), &info, &cmd);
#ifdef VULKAN_DEBUG
			VK_ASSERT(in_flight.find(cmd) == end(in_flight));
			in_flight.insert(cmd);
#endif
			buffers.push_back(cmd);
			index++;
			return cmd;
		}
	}

	void CommandPool::Begin()
	{
#ifdef VULKAN_DEBUG
		VK_ASSERT(in_flight.empty());
#endif
		if (index > 0 || secondary_index > 0)
			table->vkResetCommandPool(device->GetDevice(), pool, 0);
		index = 0;
		secondary_index = 0;
	}
}