#include "memory.hpp"

namespace vkq
{

    explicit MemoryAllocator::MemoryAllocator(MemoryAllocator::Impl* impl)
        : impl(impl)
    {
    }

    MemoryAllocator MemoryAllocator::create(const Device& device, vk::DeviceSize prefferedLargeHeapBlockSize)
    {
        MemoryAllocator::Impl* impl = new MemoryAllocator::Impl();
        impl->device = device;

        const vk::DispatchLoaderDynamic& dispatch = device.dispatch();

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

        const auto& support = device.extensionSupport();

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

        vk::Result res = static_cast<vk::Result>(vmaCreateAllocator(&createInfo, &impl->allocator));

        switch (res)
        {
        case vk::Result::eErrorFeatureNotPresent:
            throw vk::FeatureNotPresentError("vkq::MemoryAllocator::create");
        case vk::Result::eErrorInitializationFailed:
            throw vk::InitializationFailedError("vkq::MemoryAllocator::crate");
        default:
            break;
        }

        return MemoryAllocator{impl};
    }

    void MemoryAllocator::destroy()
    {
        vmaDestroyAllocator(impl->allocator);

        delete impl;
        impl = nullptr;
    }

    Device MemoryAllocator::device() const
    {
        return impl->device;
    }

    static void throwMapMemoryException(vk::Result result, const char* message)
    {
        switch (result)
        {
        case vk::Result::eErrorMemoryMapFailed:
            throw vk::MemoryMapFailedError(message);
        case vk::Result::eErrorOutOfHostMemory:
            throw vk::OutOfHostMemoryError(message);
        case vk::Result::eErrorOutOfDeviceMemory:
            throw vk::OutOfDeviceMemoryError(message);
        default:
            break;
        }
    }

    void Buffer::mapMemory(MapMemoryAccessFlags flags)
    {
        vk::Result res = static_cast<vk::Result>(vmaMapMemory(allocator_, allocation_, nullptr));
        throwMapMemoryException(res, "vkq::Buffer:mapMemory");

        if ((flags & MapMemoryAccessFlagBits::eRead) && !memoryHasPropertyFlags(vk::MemoryPropertyFlagBits::eHostCoherent))
        {
            res = static_cast<vk::Result>(vmaInvalidateAllocation(allocator_, allocation_, 0, VK_WHOLE_SIZE));
            throwMapMemoryException(res, "vkq::Buffer:mapMemory");
        }
    }

    void Buffer::unmapMemory(MapMemoryAccessFlags flags)
    {
        if ((flags & MapMemoryAccessFlagBits::eWrite) && !memoryHasPropertyFlags(vk::MemoryPropertyFlagBits::eHostCoherent))
        {
            vk::Result res = static_cast<vk::Result>(vmaFlushAllocation(allocator_, allocation_, 0, VK_WHOLE_SIZE));
            throwMapMemoryException(res, "vkq::Buffer:mapMemory");
        }

        vmaUnmapMemory(allocator_, allocation_);
    }

    void* Buffer::hostMemory()
    {
        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo(allocator_.vmaAllocator(), allocation_, &allocInfo);

        return allocInfo.pMappedData;
    }

} // namespace vkq