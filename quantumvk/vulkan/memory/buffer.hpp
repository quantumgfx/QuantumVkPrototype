#pragma once

#include "quantumvk/vulkan/misc/cookie.hpp"
#include "quantumvk/vulkan/vulkan_common.hpp"
#include "quantumvk/vulkan/vulkan_headers.hpp"
#include "quantumvk/vulkan/memory/memory_allocator.hpp"

namespace Vulkan
{
	//Foward declare Device
	class Device;

	//Determines possible command stages the buffer is used in from usage
	static inline VkPipelineStageFlags BufferUsageToPossibleStages(VkBufferUsageFlags usage)
	{
		VkPipelineStageFlags flags = 0;
		if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
			flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		if (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
			flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
		if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
			flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
		if (usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
			VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
			flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
			flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

		return flags;
	}

	//Determines possible memory access the buffer has from usage
	static inline VkAccessFlags BufferUsageToPossibleAccess(VkBufferUsageFlags usage)
	{
		VkAccessFlags flags = 0;
		if (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
			flags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
			flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
			flags |= VK_ACCESS_INDEX_READ_BIT;
		if (usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
			flags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
			flags |= VK_ACCESS_UNIFORM_READ_BIT;
		if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
			flags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

		return flags;
	}

	//Represents possible types of buffer memory
	enum class BufferDomain
	{
		Device, // Device local. Probably not visible from CPU.
		LinkedDeviceHost, // On desktop, directly mapped VRAM over PCI.
		Host, // Host-only, needs to be synced to GPU. Prefer coherent Might be device local as well on iGPUs.
		CachedHost, //Host visible and host cached
	};

	//Flags for BufferCreateInfo
	enum BufferMiscFlagBits
	{
		BUFFER_MISC_ZERO_INITIALIZE_BIT = 1 << 0
	};

	using BufferMiscFlags = uint32_t;

	enum BufferCommandQueueFlagBits
	{
		BUFFER_COMMAND_QUEUE_GENERIC = 1 << 0,
		BUFFER_COMMAND_QUEUE_ASYNC_GRAPHICS = 1 << 1,
		BUFFER_COMMAND_QUEUE_ASYNC_COMPUTE = 1 << 2,
		BUFFER_COMMAND_QUEUE_ASYNC_TRANSFER = 1 << 3,
	};

	using BufferCommandQueueFlags = uint32_t;

	enum class BufferSharingMode
	{
		Concurrent = 0,
		Exclusive
	};

	//Info on how to create a buffer
	struct BufferCreateInfo
	{
		//Memory type of buffer
		BufferDomain domain = BufferDomain::Device;
		//Size of buffer
		VkDeviceSize size = 0;
		//Usage of buffer
		VkBufferUsageFlags usage = 0;
		//Misc buffer flags
		BufferMiscFlags misc = 0;

		BufferSharingMode sharing_mode = BufferSharingMode::Concurrent;
		BufferCommandQueueFlagBits exclusive_owner = BUFFER_COMMAND_QUEUE_GENERIC;
		BufferCommandQueueFlags concurrent_owners = BUFFER_COMMAND_QUEUE_GENERIC | BUFFER_COMMAND_QUEUE_ASYNC_GRAPHICS | BUFFER_COMMAND_QUEUE_ASYNC_COMPUTE | BUFFER_COMMAND_QUEUE_ASYNC_TRANSFER;
	};

	struct BufferAllocation
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		DeviceAllocation alloc;
	};

	//Forward declaration of buffer
	class Buffer;
	//Buffer deletion functor
	struct BufferDeleter
	{
		void operator()(Buffer* buffer);
	};

	//Forward declaration of buffer view
	class BufferView;
	//BufferView deletion functor
	struct BufferViewDeleter
	{
		void operator()(BufferView* view);
	};

	class Buffer : public Util::IntrusivePtrEnabled<Buffer, BufferDeleter, HandleCounter>, public Cookie, public InternalSyncEnabled
	{
	public:
		friend struct BufferDeleter;
		//Delays the deletion of the buffer/freeing of memory until current frame context is finished or device is destroyed
		~Buffer();

		//Return the buffer's VkBuffer
		VkBuffer GetBuffer() const
		{
			return buffer;
		}

		//Return the buffer's create info
		const BufferCreateInfo& GetCreateInfo() const
		{
			return info;
		}

		//Return the buffer's memory allocation
		DeviceAllocation& GetAllocation()
		{
			return alloc;
		}

		//Return the buffers memory alloction
		const DeviceAllocation& GetAllocation() const
		{
			return alloc;
		}

	private:
		friend class Util::ObjectPool<Buffer>;
		Buffer(Device* device, VkBuffer buffer, const DeviceAllocation& alloc, const BufferCreateInfo& info);

		Device* device;
		VkBuffer buffer;
		DeviceAllocation alloc;
		BufferCreateInfo info;
	};
	using BufferHandle = Util::IntrusivePtr<Buffer>;

	//Info detailing creation of buffer view
	struct BufferViewCreateInfo
	{
		//Buffer that View was created from
		const Buffer* buffer;
		//Format of view
		VkFormat format;
		//Offset in original buffer
		VkDeviceSize offset;
		//Range within original buffer
		VkDeviceSize range;
	};

	class BufferView : public Util::IntrusivePtrEnabled<BufferView, BufferViewDeleter, HandleCounter>, public Cookie, public InternalSyncEnabled
	{
	public:
		friend struct BufferViewDeleter;
		~BufferView();

		//Get the VkBufferView
		VkBufferView GetView() const
		{
			return view;
		}

		//Get the bufferViewCreateInfo
		const BufferViewCreateInfo& GetCreateInfo()
		{
			return info;
		}

		//Get the buffer the buffer view belongs to
		const Buffer& GetBuffer() const
		{
			return *info.buffer;
		}

	private:
		friend class Util::ObjectPool<BufferView>;
		BufferView(Device* device, VkBufferView view, const BufferViewCreateInfo& info);

		Device* device;
		VkBufferView view;
		BufferViewCreateInfo info;
	};
	using BufferViewHandle = Util::IntrusivePtr<BufferView>;

}