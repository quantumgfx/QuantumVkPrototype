#include "memory.hpp"

namespace vkq
{

    MemoryAllocator MemoryAllocator::create(const Device& device)
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

        VmaAllocatorCreateInfo createInfo{};
    }

} // namespace vkq