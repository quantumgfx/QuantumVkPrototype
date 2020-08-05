#pragma once

#include "utils/intrusive.hpp"
#include "utils/object_pool.hpp"
#include "utils/intrusive_list.hpp"
#include "vulkan_headers.hpp"
#include "utils/logging.hpp"
#include "utils/bitops.hpp"
#include <assert.h>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <vector>

#include <VulkanMemoryAllocator/vk_mem_alloc.h>

#ifdef QM_VULKAN_MT
#include <mutex>
#endif

namespace Vulkan
{
	//Forward declare device
	class Device;

	enum MemoryAccessFlag
	{
		MEMORY_ACCESS_WRITE_BIT = 1,
		MEMORY_ACCESS_READ_BIT = 2,
		MEMORY_ACCESS_READ_WRITE_BIT = MEMORY_ACCESS_WRITE_BIT | MEMORY_ACCESS_READ_BIT
	};

	using MemoryAccessFlags = uint32_t;

	struct DeviceAllocation
	{
		VmaAllocation vmaAllocation = VK_NULL_HANDLE;
	};

	class DeviceAllocator
	{
	public:

		void Init(Device* device);

		~DeviceAllocator();

		//Allocate Memory for new buffer, create the buffer and bind the memory to it
		bool AllocateBuffer(const VkBufferCreateInfo& buffer_create_info, const VmaAllocationCreateInfo& mem_alloc_create_info, VkBuffer* buffer, DeviceAllocation* allocation);
		//Allocate Memory for new image, create the image, and bind the memory to it
		bool AllocateImage(const VkImageCreateInfo& buffer_create_info, const VmaAllocationCreateInfo& mem_alloc_create_info, VkImage* image, DeviceAllocation* allocation);

		//Destroy and Free Buffer
		void FreeBuffer(VkBuffer buffer, DeviceAllocation allocation);
		//Destroy and Free Image
		void FreeImage(VkImage image, DeviceAllocation allocation);

		//Map Allocation memory
		void MapMemory(const DeviceAllocation& alloc, void** p_data);
		//Unmap Allocation memory
		void UnmapMemory(const DeviceAllocation& alloc);

		//Invalidate memory
		void InvalidateMemory(const DeviceAllocation& alloc, VkDeviceSize offset, VkDeviceSize size);
		//Flush memory
		void FlushMemory(const DeviceAllocation& alloc, VkDeviceSize offset, VkDeviceSize size);

	private:

		VmaAllocator allocator;
#ifdef QM_VULKAN_MT
		std::mutex m_mutex;
#endif

	};
}