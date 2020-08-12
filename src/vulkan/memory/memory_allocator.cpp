#include "memory_allocator.hpp"
#include "vulkan/device.hpp"

namespace Vulkan
{
	void DeviceAllocator::Init(Device* device)
	{
#ifdef QM_VULKAN_MT
		std::lock_guard lock(m_mutex);
#endif
		const VolkDeviceTable& table = device->GetDeviceTable();

		VmaVulkanFunctions vulkan_function_ptrs;
		vulkan_function_ptrs.vkAllocateMemory =                        table.vkAllocateMemory;
		vulkan_function_ptrs.vkBindBufferMemory =                      table.vkBindBufferMemory;
		vulkan_function_ptrs.vkBindBufferMemory2KHR =                  table.vkBindBufferMemory2KHR;
		vulkan_function_ptrs.vkBindImageMemory =                       table.vkBindImageMemory;
		vulkan_function_ptrs.vkBindImageMemory2KHR =                   table.vkBindImageMemory2KHR;
		vulkan_function_ptrs.vkCmdCopyBuffer =                         table.vkCmdCopyBuffer;
		vulkan_function_ptrs.vkCreateBuffer =                          table.vkCreateBuffer;
		vulkan_function_ptrs.vkCreateImage =                           table.vkCreateImage;
		vulkan_function_ptrs.vkDestroyBuffer =                         table.vkDestroyBuffer;
		vulkan_function_ptrs.vkDestroyImage =                          table.vkDestroyImage;
		vulkan_function_ptrs.vkFlushMappedMemoryRanges =               table.vkFlushMappedMemoryRanges;
		vulkan_function_ptrs.vkFreeMemory =                            table.vkFreeMemory;
		vulkan_function_ptrs.vkGetBufferMemoryRequirements =           table.vkGetBufferMemoryRequirements;
		vulkan_function_ptrs.vkGetBufferMemoryRequirements2KHR =       table.vkGetBufferMemoryRequirements2KHR;
		vulkan_function_ptrs.vkGetImageMemoryRequirements =            table.vkGetImageMemoryRequirements;
		vulkan_function_ptrs.vkGetImageMemoryRequirements2KHR =        table.vkGetImageMemoryRequirements2KHR;
		vulkan_function_ptrs.vkGetPhysicalDeviceMemoryProperties =     vkGetPhysicalDeviceMemoryProperties;
		vulkan_function_ptrs.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2KHR;
		vulkan_function_ptrs.vkGetPhysicalDeviceProperties =           vkGetPhysicalDeviceProperties;
		vulkan_function_ptrs.vkInvalidateMappedMemoryRanges =          table.vkInvalidateMappedMemoryRanges;
		vulkan_function_ptrs.vkMapMemory =                             table.vkMapMemory;
		vulkan_function_ptrs.vkUnmapMemory =                           table.vkUnmapMemory;

		VmaAllocatorCreateInfo create_info{};
		create_info.flags = 0;
		create_info.frameInUseCount = 0;
		create_info.pAllocationCallbacks = nullptr;
		create_info.pDeviceMemoryCallbacks = nullptr;
		create_info.pRecordSettings = nullptr;
		create_info.pVulkanFunctions = &vulkan_function_ptrs;
		if (device->GetDeviceFeatures().supports_vulkan_11_device || device->GetDeviceFeatures().supports_vulkan_12_device)
			create_info.vulkanApiVersion = VK_MAKE_VERSION(1, 1, 0);
		else
			create_info.vulkanApiVersion = VK_MAKE_VERSION(1, 0, 0);
		create_info.instance = device->GetInstance();
		create_info.physicalDevice = device->GetPhysicalDevice();
		create_info.device = device->GetDevice();

		vmaCreateAllocator(&create_info, &allocator);
	}

	DeviceAllocator::~DeviceAllocator()
	{
#ifdef QM_VULKAN_MT
		std::lock_guard lock(m_mutex);
#endif

		vmaDestroyAllocator(allocator);
	}

	bool DeviceAllocator::AllocateBuffer(const VkBufferCreateInfo& buffer_create_info, const VmaAllocationCreateInfo& mem_alloc_create_info, VkBuffer* buffer, DeviceAllocation* allocation)
	{
#ifdef QM_VULKAN_MT
		std::lock_guard lock(m_mutex);
#endif

		VmaAllocationInfo alloc_info{};

		if (vmaCreateBuffer(allocator, &buffer_create_info, &mem_alloc_create_info, buffer, &allocation->vma_allocation, &alloc_info) == VK_SUCCESS)
		{

			allocation->size = buffer_create_info.size;
			allocation->mem_type = alloc_info.memoryType;
			allocation->host_base = (uint8_t*)alloc_info.pMappedData;
			allocation->persistantly_mapped = (mem_alloc_create_info.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT);
			return true;
		}
		return false;
	}

	bool DeviceAllocator::AllocateImage(const VkImageCreateInfo& image_create_info, const VmaAllocationCreateInfo& mem_alloc_create_info, VkImage* image, DeviceAllocation* allocation)
	{
#ifdef QM_VULKAN_MT
		std::lock_guard lock(m_mutex);
#endif

		VmaAllocationInfo alloc_info{};

		if (vmaCreateImage(allocator, &image_create_info, &mem_alloc_create_info, image, &allocation->vma_allocation, &alloc_info) == VK_SUCCESS)
		{
			allocation->size = alloc_info.size;
			allocation->mem_type = alloc_info.memoryType;
			allocation->host_base = (uint8_t*)alloc_info.pMappedData;
			allocation->persistantly_mapped = (mem_alloc_create_info.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT);
			return true;
		}
		return false;
	}

	void DeviceAllocator::FreeBuffer(VkBuffer buffer, const DeviceAllocation& allocation)
	{
#ifdef QM_VULKAN_MT
		std::lock_guard lock(m_mutex);
#endif

		vmaDestroyBuffer(allocator, buffer, allocation.vma_allocation);
	}

	void DeviceAllocator::FreeImage(VkImage image, const DeviceAllocation& allocation)
	{
#ifdef QM_VULKAN_MT
		std::lock_guard lock(m_mutex);
#endif

		vmaDestroyImage(allocator, image, allocation.vma_allocation);
	}

	void* DeviceAllocator::MapMemory(const DeviceAllocation& alloc, MemoryAccessFlags flags)
	{
#ifdef QM_VULKAN_MT
		std::lock_guard lock(m_mutex);
#endif

		if (!HasMemoryPropertyFlags(alloc, mem_props, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
		{
			//If memory isn't host visible it can't be mapped on the host
			return nullptr;
		}

		if (!alloc.persistantly_mapped)
		{
			void* p_data;
			if (vmaMapMemory(allocator, alloc.vma_allocation, &p_data) != VK_SUCCESS)
			{
				QM_LOG_ERROR("Failed to map memory");
				return nullptr;
			}
			alloc.host_base = (uint8_t*)p_data;
		}

		if ((flags & MEMORY_ACCESS_READ_BIT) && !HasMemoryPropertyFlags(alloc, mem_props, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		{
			vmaInvalidateAllocation(allocator, alloc.vma_allocation, 0, VK_WHOLE_SIZE);
		}

		return alloc.host_base;
	}

	void DeviceAllocator::UnmapMemory(const DeviceAllocation& alloc, MemoryAccessFlags flags)
	{
#ifdef QM_VULKAN_MT
		std::lock_guard lock(m_mutex);
#endif

		if (!HasMemoryPropertyFlags(alloc, mem_props, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
		{
			//If memory isn't host visible it can't be mapped on the host
			return;
		}

		if (!alloc.persistantly_mapped)
		{
			vmaUnmapMemory(allocator, alloc.vma_allocation);
		}

		{
			VmaAllocationInfo alloc_info{};
			vmaGetAllocationInfo(allocator, alloc.vma_allocation, &alloc_info);
			alloc.host_base = (uint8_t*)alloc_info.pMappedData;
		}

		if ((flags & MEMORY_ACCESS_WRITE_BIT) && !HasMemoryPropertyFlags(alloc, mem_props, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		{
			vmaFlushAllocation(allocator, alloc.vma_allocation, 0, VK_WHOLE_SIZE);
		}
	}
}