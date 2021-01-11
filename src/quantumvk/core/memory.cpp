#include "memory.hpp"

namespace vkq
{

    MemoryAllocator MemoryAllocator::create(const Device& device, vk::DeviceSize prefferedLargeHeapBlockSize)
    {
        const vk::DispatchLoaderDynamic& dispatch = device.getDeviceDispatch();

        VmaVulkanFunctions funcs{};
        funcs.vkAllocateMemory = dispatch.vkAllocateMemory;
        funcs.vkBindBufferMemory = dispatch.vkBindBufferMemory;
        funcs.vkBindImageMemory = dispatch.vkBindImageMemory;
        funcs.vkCmdCopyBuffer = dispatch.vkCmdCopyBuffer;
        funcs.vkCreateBuffer = dispatch.vkCreateBuffer;
        funcs.vkCreateImage = dispatch.vkCreateImage;
        funcs.vkDestroyBuffer = dispatch.vkDestroyBuffer;
        funcs.vkDestroyImage = dispatch.vkDestroyImage;
        funcs.vkFlushMappedMemoryRanges = dispatch.vkFlushMappedMemoryRanges;
        funcs.vkFreeMemory = dispatch.vkFreeMemory;
        funcs.vkGetBufferMemoryRequirements = dispatch.vkGetBufferMemoryRequirements;
        funcs.vkGetImageMemoryRequirements = dispatch.vkGetImageMemoryRequirements;
        funcs.vkGetPhysicalDeviceMemoryProperties = dispatch.vkGetPhysicalDeviceMemoryProperties;
        funcs.vkInvalidateMappedMemoryRanges = dispatch.vkInvalidateMappedMemoryRanges;
        funcs.vkMapMemory = dispatch.vkMapMemory;
        funcs.vkUnmapMemory = dispatch.vkUnmapMemory;

        //funcs.vkBindBufferMemory2KHR = dispatch.vkBindBufferMemory2KHR;
        //funcs.vkBindImageMemory2KHR = dispatch.vkBindImageMemory2KHR;
        //funcs.vkGetBufferMemoryRequirements2KHR = dispatch.vkGetBufferMemoryRequirements2KHR;
        //funcs.vkGetImageMemoryRequirements2KHR = dispatch.vkGetImageMemoryRequirements2KHR;
        //funcs.vkGetPhysicalDeviceMemoryProperties2KHR = dispatch.vkGetPhysicalDeviceMemoryProperties2KHR;

        VmaAllocatorCreateFlags flags = 0;

        const auto& support = device.getDeviceExtensionSupport();

        if (support.bindMemory2KHR)
            flags |= VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT;
        if (support.bufferDeviceAddressKHR)
            flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        if (support.dedicatedAllocationKHR)
            flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
        if (support.deviceCoherentMemoryAMD)
            flags |= VMA_ALLOCATOR_CREATE_AMD_DEVICE_COHERENT_MEMORY_BIT;
        if (support.memoryBudgetEXT)
            flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;

        VmaAllocatorCreateInfo createInfo{};
        createInfo.instance = device.vkInstance();
        createInfo.physicalDevice = device.vkPhysicalDevice();
        createInfo.device = device.vkDevice();
        createInfo.pVulkanFunctions = &funcs;
        createInfo.pRecordSettings = nullptr;
        createInfo.pAllocationCallbacks = nullptr;
        createInfo.pHeapSizeLimit = nullptr;
        createInfo.frameInUseCount = 0;
        createInfo.preferredLargeHeapBlockSize = prefferedLargeHeapBlockSize;
        createInfo.flags = flags;
        createInfo.vulkanApiVersion = VK_MAKE_VERSION(1, 0, 0);

        VmaAllocator allocator;

        vk::Result result = static_cast<vk::Result>(vmaCreateAllocator(&createInfo, &allocator));

        switch (result)
        {
        case vk::Result::eErrorFeatureNotPresent:
            throw vk::FeatureNotPresentError("vkq::MemoryAllocator::create");
        case vk::Result::eErrorInitializationFailed:
            throw vk::InitializationFailedError("vkq::MemoryAllocator::crate");
        default:
            break;
        }
    }

} // namespace vkq