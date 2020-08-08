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
		VmaAllocation vma_allocation = VK_NULL_HANDLE;
		//Size of allocation. Valid for buffers, but should not be used by image allocations (except for memory mapping).
		VkDeviceSize size = 0;
		uint32_t mem_type = 0;
		mutable uint8_t* host_base = nullptr;
		bool persistantly_mapped = false;
	};

	inline bool HasMemoryPropertyFlags(const DeviceAllocation& alloc, const VkPhysicalDeviceMemoryProperties& mem_props, VkMemoryPropertyFlags flags)
	{
		return mem_props.memoryTypes[alloc.mem_type].propertyFlags & flags;
	}

	class DeviceAllocator
	{
	public:

		//Inits and creates the device allocator
		void Init(Device* device);
		//Cleans up the device allocator
		~DeviceAllocator();

		//Allocate Memory for new buffer, create the buffer and bind the memory to it
		bool AllocateBuffer(const VkBufferCreateInfo& buffer_create_info, const VmaAllocationCreateInfo& mem_alloc_create_info, VkBuffer* buffer, DeviceAllocation* allocation);
		//Allocate Memory for new image, create the image, and bind the memory to it
		bool AllocateImage(const VkImageCreateInfo& image_create_info, const VmaAllocationCreateInfo& mem_alloc_create_info, VkImage* image, DeviceAllocation* allocation);

		//Destroy and Free Buffer
		void FreeBuffer(VkBuffer buffer, const DeviceAllocation& allocation);
		//Destroy and Free Image
		void FreeImage(VkImage image, const DeviceAllocation& allocation);

		//Map Allocation memory
		void* MapMemory(const DeviceAllocation& alloc, MemoryAccessFlags flags);
		//Unmap Allocation memory
		void UnmapMemory(const DeviceAllocation& alloc, MemoryAccessFlags flags);

	private:

		VmaAllocator allocator = VK_NULL_HANDLE;
		VkPhysicalDeviceMemoryProperties mem_props{};
#ifdef QM_VULKAN_MT
		std::mutex m_mutex;
#endif

	};
}