#include "memory.hpp"

#include "../base/common.hpp"

#include <mutex>

namespace vkq
{
    ///////////////////////////////////
    // Buffer /////////////////////////
    ///////////////////////////////////

    explicit Buffer::Buffer(Buffer::Impl* impl)
        : impl(impl)
    {
    }

    Buffer Buffer::create(const LinearMemoryPool& linearPool, const vk::BufferCreateInfo& createInfo, LinearAllocationFlags allocFlags)
    {
        const auto& allocator = linearPool.allocator();

        Impl* impl = allocator.allocBufferHandle();
        impl->hostMemory = nullptr;
        impl->size = createInfo.size;
        impl->usage = createInfo.usage;

        VmaAllocationCreateFlags vmaAllocFlags = 0;

        if (allocFlags & LinearAllocationFlagBits::eMapped)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (allocFlags & LinearAllocationFlagBits::eUpperAddress)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.pool = linearPool.vmaPool();
        allocInfo.pUserData = nullptr;
        allocInfo.flags = vmaAllocFlags;

        VmaAllocationInfo info{};
        vk::Result res = static_cast<vk::Result>(vmaCreateBuffer(allocator.vmaAllocator(), reinterpret_cast<const VkBufferCreateInfo*>(&createInfo), &allocInfo, reinterpret_cast<VkBuffer*>(&impl->buffer), &impl->allocation, &info));
        vk::throwResultException(res, "vkq::Buffer::create");

        impl->memoryTypeIndex = info.memoryType;

        return Buffer{impl};
    }

    Buffer Buffer::create(const MemoryPool& pool, const vk::BufferCreateInfo& createInfo, PoolAllocationFlags allocFlags, AllocationStrategy strategy)
    {
        const auto& allocator = pool.allocator();

        Impl* impl = allocator.allocBufferHandle();
        impl->hostMemory = nullptr;
        impl->size = createInfo.size;
        impl->usage = createInfo.usage;

        VmaAllocationCreateFlags vmaAllocFlags = 0;

        if (allocFlags & PoolAllocationFlagBits::eMapped)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (allocFlags & PoolAllocationFlagBits::eNeverAllocate)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT;
        if (allocFlags & PoolAllocationFlagBits::eWithinBudget)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;

        if (strategy == AllocationStrategy::eStrategyMinMemory)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
        else if (strategy == AllocationStrategy::eStrategyMinTime)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
        else if (strategy == AllocationStrategy::eStrategyMinFragmentation)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_FRAGMENTATION_BIT;
        else // Default
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.pool = pool.vmaPool();
        allocInfo.pUserData = nullptr;
        allocInfo.flags = vmaAllocFlags;

        VmaAllocationInfo info{};
        vk::Result res = static_cast<vk::Result>(vmaCreateBuffer(allocator.vmaAllocator(), reinterpret_cast<const VkBufferCreateInfo*>(&createInfo), &allocInfo, reinterpret_cast<VkBuffer*>(&impl->buffer), &impl->allocation, &info));
        vk::throwResultException(res, "vkq::Buffer::create");

        impl->memoryTypeIndex = info.memoryType;

        return Buffer{impl};
    }

    Buffer Buffer::create(const MemoryAllocator& allocator, const vk::BufferCreateInfo& createInfo, vk::MemoryPropertyFlags requiredMemFlags, vk::MemoryPropertyFlags preferredMemFlags, AllocationFlags allocFlags, AllocationStrategy strategy, uint32_t memoryTypeBits)
    {
        Impl* impl = allocator.allocBufferHandle();
        impl->hostMemory = nullptr;
        impl->size = createInfo.size;
        impl->usage = createInfo.usage;

        VmaAllocationCreateFlags vmaAllocFlags = 0;

        if (allocFlags & AllocationFlagBits::eMapped)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (allocFlags & AllocationFlagBits::eNeverAllocate)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT;
        if (allocFlags & AllocationFlagBits::eWithinBudget)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
        if (allocFlags & AllocationFlagBits::eDedicatedMemory)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (strategy == AllocationStrategy::eStrategyMinMemory)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
        else if (strategy == AllocationStrategy::eStrategyMinTime)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
        else if (strategy == AllocationStrategy::eStrategyMinFragmentation)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_FRAGMENTATION_BIT;
        else // Default
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.pool = VMA_NULL;
        allocInfo.pUserData = nullptr;
        allocInfo.flags = vmaAllocFlags;
        allocInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
        allocInfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(requiredMemFlags);
        allocInfo.preferredFlags = static_cast<VkMemoryPropertyFlags>(preferredMemFlags);
        allocInfo.memoryTypeBits = (memoryTypeBits == 0) ? UINT32_MAX : memoryTypeBits;

        VmaAllocationInfo info{};
        vk::Result res = static_cast<vk::Result>(vmaCreateBuffer(allocator.vmaAllocator(), reinterpret_cast<const VkBufferCreateInfo*>(&createInfo), &allocInfo, reinterpret_cast<VkBuffer*>(&impl->buffer), &impl->allocation, &info));
        vk::throwResultException(res, "vkq::Buffer::create");

        impl->memoryTypeIndex = info.memoryType;

        return Buffer{impl};
    }

    void Buffer::destroy()
    {
        MemoryAllocator allocator = impl->allocator;
        allocator.freeBufferHandle(impl);

        impl = nullptr;
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
        vk::Result res = static_cast<vk::Result>(vmaMapMemory(impl->allocator, impl->allocation, &impl->hostMemory));
        throwMapMemoryException(res, "vkq::Buffer:mapMemory");

        if ((flags & MapMemoryAccessFlagBits::eRead) && !memoryHasPropertyFlags(vk::MemoryPropertyFlagBits::eHostCoherent))
        {
            res = static_cast<vk::Result>(vmaInvalidateAllocation(impl->allocator, impl->allocation, 0, VK_WHOLE_SIZE));
            throwMapMemoryException(res, "vkq::Buffer:mapMemory");
        }
    }

    void Buffer::unmapMemory(MapMemoryAccessFlags flags)
    {
        if ((flags & MapMemoryAccessFlagBits::eWrite) && !memoryHasPropertyFlags(vk::MemoryPropertyFlagBits::eHostCoherent))
        {
            vk::Result res = static_cast<vk::Result>(vmaFlushAllocation(impl->allocator, impl->allocation, 0, VK_WHOLE_SIZE));
            throwMapMemoryException(res, "vkq::Buffer:mapMemory");
        }

        vmaUnmapMemory(impl->allocator, impl->allocation);

        impl->hostMemory = nullptr;
    }

    void* Buffer::hostMemory()
    {
        return impl->hostMemory;
    }

    Device Buffer::device() const
    {
        return impl->allocator.device();
    }

    MemoryAllocator Buffer::allocator() const
    {
        return impl->allocator;
    }

    VmaAllocation Buffer::vmaAllocation() const
    {
        return impl->allocation;
    }

    vk::Buffer Buffer::vkBuffer() const
    {
        return impl->buffer;
    }

    vk::Buffer Buffer::vkHandle() const
    {
        return impl->buffer;
    }

    Buffer::operator vk::Buffer() const
    {
        return impl->buffer;
    }

    ///////////////////////////////////
    // Image //////////////////////////
    ///////////////////////////////////

    explicit Image::Image(Image::Impl* impl)
        : impl(impl)
    {
    }

    Image Image::create(const LinearMemoryPool& linearPool, const vk::ImageCreateInfo& createInfo, LinearAllocationFlags allocFlags)
    {
        const auto& allocator = linearPool.allocator();

        Image::Impl* impl = allocator.allocImageHandle();
        impl->hostMemory = nullptr;
        impl->imageType = createInfo.imageType;
        impl->format = createInfo.format;
        impl->extent = createInfo.extent;
        impl->mipLevels = createInfo.mipLevels;
        impl->arrayLayers = createInfo.arrayLayers;
        impl->samples = createInfo.samples;
        impl->tiling = createInfo.tiling;
        impl->usage = createInfo.usage;

        VmaAllocationCreateFlags vmaAllocFlags = 0;

        if (allocFlags & LinearAllocationFlagBits::eMapped)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (allocFlags & LinearAllocationFlagBits::eUpperAddress)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.pool = linearPool.vmaPool();
        allocInfo.pUserData = nullptr;
        allocInfo.flags = vmaAllocFlags;

        VmaAllocationInfo info{};
        vk::Result res = static_cast<vk::Result>(vmaCreateImage(allocator.vmaAllocator(), reinterpret_cast<const VkImageCreateInfo*>(&createInfo), &allocInfo, reinterpret_cast<VkImage*>(&impl->image), &impl->allocation, &info));
        vk::throwResultException(res, "vkq::Buffer::create");

        impl->memoryTypeIndex = info.memoryType;

        return Image{impl};
    }

    Image Image::create(const MemoryPool& pool, const vk::ImageCreateInfo& createInfo, PoolAllocationFlags allocFlags, AllocationStrategy strategy)
    {
        const auto& allocator = pool.allocator();

        Image::Impl* impl = allocator.allocImageHandle();
        impl->hostMemory = nullptr;
        impl->imageType = createInfo.imageType;
        impl->format = createInfo.format;
        impl->extent = createInfo.extent;
        impl->mipLevels = createInfo.mipLevels;
        impl->arrayLayers = createInfo.arrayLayers;
        impl->samples = createInfo.samples;
        impl->tiling = createInfo.tiling;
        impl->usage = createInfo.usage;

        VmaAllocationCreateFlags vmaAllocFlags = 0;

        if (allocFlags & PoolAllocationFlagBits::eMapped)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (allocFlags & PoolAllocationFlagBits::eNeverAllocate)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT;
        if (allocFlags & PoolAllocationFlagBits::eWithinBudget)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;

        if (strategy == AllocationStrategy::eStrategyMinMemory)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
        else if (strategy == AllocationStrategy::eStrategyMinTime)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
        else if (strategy == AllocationStrategy::eStrategyMinFragmentation)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_FRAGMENTATION_BIT;
        else // Default
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.pool = pool.vmaPool();
        allocInfo.pUserData = nullptr;
        allocInfo.flags = vmaAllocFlags;

        VmaAllocationInfo info{};
        vk::Result res = static_cast<vk::Result>(vmaCreateImage(allocator.vmaAllocator(), reinterpret_cast<const VkImageCreateInfo*>(&createInfo), &allocInfo, reinterpret_cast<VkImage*>(&impl->image), &impl->allocation, &info));
        vk::throwResultException(res, "vkq::Buffer::create");

        impl->memoryTypeIndex = info.memoryType;

        return Image{impl};
    }

    Image Image::create(const MemoryAllocator& allocator, const vk::ImageCreateInfo& createInfo, vk::MemoryPropertyFlags requiredMemFlags, vk::MemoryPropertyFlags preferredMemFlags, AllocationFlags allocFlags, AllocationStrategy strategy, uint32_t memoryTypeBits)
    {
        Image::Impl* impl = allocator.allocImageHandle();
        impl->hostMemory = nullptr;
        impl->imageType = createInfo.imageType;
        impl->format = createInfo.format;
        impl->extent = createInfo.extent;
        impl->mipLevels = createInfo.mipLevels;
        impl->arrayLayers = createInfo.arrayLayers;
        impl->samples = createInfo.samples;
        impl->tiling = createInfo.tiling;
        impl->usage = createInfo.usage;

        VmaAllocationCreateFlags vmaAllocFlags = 0;

        if (allocFlags & AllocationFlagBits::eMapped)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (allocFlags & AllocationFlagBits::eNeverAllocate)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT;
        if (allocFlags & AllocationFlagBits::eWithinBudget)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
        if (allocFlags & AllocationFlagBits::eDedicatedMemory)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (strategy == AllocationStrategy::eStrategyMinMemory)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
        else if (strategy == AllocationStrategy::eStrategyMinTime)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
        else if (strategy == AllocationStrategy::eStrategyMinFragmentation)
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_FRAGMENTATION_BIT;
        else // Default
            vmaAllocFlags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.pool = VMA_NULL;
        allocInfo.pUserData = nullptr;
        allocInfo.flags = vmaAllocFlags;
        allocInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;
        allocInfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(requiredMemFlags);
        allocInfo.preferredFlags = static_cast<VkMemoryPropertyFlags>(preferredMemFlags);
        allocInfo.memoryTypeBits = (memoryTypeBits == 0) ? UINT32_MAX : memoryTypeBits;

        VmaAllocationInfo info{};
        vk::Result res = static_cast<vk::Result>(vmaCreateImage(allocator.vmaAllocator(), reinterpret_cast<const VkImageCreateInfo*>(&createInfo), &allocInfo, reinterpret_cast<VkImage*>(&impl->image), &impl->allocation, &info));
        vk::throwResultException(res, "vkq::Buffer::create");

        impl->memoryTypeIndex = info.memoryType;

        return Image{impl};
    }

    void Image::destroy()
    {
        MemoryAllocator allocator = impl->allocator;
        allocator.freeImageHandle(impl);

        impl = nullptr;
    }

    void Image::mapMemory(MapMemoryAccessFlags flags)
    {
        vk::Result res = static_cast<vk::Result>(vmaMapMemory(impl->allocator, impl->allocation, &impl->hostMemory));
        throwMapMemoryException(res, "vkq::Buffer:mapMemory");

        if ((flags & MapMemoryAccessFlagBits::eRead) && !memoryHasPropertyFlags(vk::MemoryPropertyFlagBits::eHostCoherent))
        {
            res = static_cast<vk::Result>(vmaInvalidateAllocation(impl->allocator, impl->allocation, 0, VK_WHOLE_SIZE));
            throwMapMemoryException(res, "vkq::Buffer:mapMemory");
        }
    }

    void Image::unmapMemory(MapMemoryAccessFlags flags)
    {
        if ((flags & MapMemoryAccessFlagBits::eWrite) && !memoryHasPropertyFlags(vk::MemoryPropertyFlagBits::eHostCoherent))
        {
            vk::Result res = static_cast<vk::Result>(vmaFlushAllocation(impl->allocator, impl->allocation, 0, VK_WHOLE_SIZE));
            throwMapMemoryException(res, "vkq::Buffer:mapMemory");
        }

        vmaUnmapMemory(impl->allocator, impl->allocation);

        impl->hostMemory = nullptr;
    }

    void* Image::hostMemory()
    {
        return impl->hostMemory;
    }

    vk::ImageType Image::imageType() const
    {
        return impl->imageType;
    }

    vk::Format Image::format() const
    {
        return impl->format;
    }

    vk::Extent3D Image::extent() const
    {
        return impl->extent;
    }

    uint32_t Image::mipLevels() const
    {
        return impl->mipLevels;
    }

    uint32_t Image::arrayLayers() const
    {
        return impl->arrayLayers;
    }

    vk::SampleCountFlagBits Image::samples() const
    {
        return impl->samples;
    }

    vk::ImageTiling Image::tiling() const
    {
        return impl->tiling;
    }

    vk::ImageUsageFlags Image::usage() const
    {
        return impl->usage;
    }

    Device Image::device() const
    {
        return impl->allocator.device();
    }

    MemoryAllocator Image::allocator() const
    {
        return impl->allocator;
    }

    VmaAllocation Image::vmaAllocation() const
    {
        return impl->allocation;
    }

    vk::Image Image::vkImage() const
    {
        return impl->image;
    }

    vk::Image Image::vkHandle() const
    {
        return impl->image;
    }

    Image::operator vk::Image() const
    {
        return impl->image;
    }

    ///////////////////////////////////
    // Memory Allocator ///////////////
    ///////////////////////////////////

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

        uint32_t instanceApiVersion = device.instance().apiVersion();
        uint32_t deviceApiVersion = device.apiVersion();
        const auto& instanceSupport = device.instance().extensionSupport();
        const auto& deviceSupport = device.extensionSupport();

        if (instanceApiVersion < VK_MAKE_VERSION(1, 1, 0))
        {
            if (instanceSupport.getPhysicalDeviceProperties2KHR)
            {
                funcs.vkGetPhysicalDeviceMemoryProperties2KHR = dispatch.vkGetPhysicalDeviceMemoryProperties2KHR;
            }
        }
#ifdef VK_VERSION_1_1
        else
        {
            funcs.vkGetPhysicalDeviceMemoryProperties2KHR = dispatch.vkGetPhysicalDeviceMemoryProperties2;
        }

#endif

        if (deviceApiVersion < VK_MAKE_VERSION(1, 1, 0))
        {
            if (deviceSupport.bindMemory2KHR)
            {
                funcs.vkBindBufferMemory2KHR = dispatch.vkBindBufferMemory2KHR;
                funcs.vkBindImageMemory2KHR = dispatch.vkBindImageMemory2KHR;
            }

            if (deviceSupport.getMemoryRequirements2KHR)
            {
                funcs.vkGetBufferMemoryRequirements2KHR = dispatch.vkGetBufferMemoryRequirements2KHR;
                funcs.vkGetImageMemoryRequirements2KHR = dispatch.vkGetImageMemoryRequirements2KHR;
            }
        }
#ifdef VK_VERSION_1_1
        else
        {
            funcs.vkBindBufferMemory2KHR = dispatch.vkBindBufferMemory2;
            funcs.vkBindImageMemory2KHR = dispatch.vkBindImageMemory2;
            funcs.vkGetBufferMemoryRequirements2KHR = dispatch.vkGetBufferMemoryRequirements2;
            funcs.vkGetImageMemoryRequirements2KHR = dispatch.vkGetImageMemoryRequirements2;
        }
#endif

        VmaAllocatorCreateFlags flags = 0;

        if (deviceSupport.bindMemory2KHR)
            flags |= VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT;
        if (deviceSupport.bufferDeviceAddressKHR)
            flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        if (deviceSupport.dedicatedAllocationKHR)
            flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
        if (deviceSupport.deviceCoherentMemoryAMD)
            flags |= VMA_ALLOCATOR_CREATE_AMD_DEVICE_COHERENT_MEMORY_BIT;
        if (deviceSupport.memoryBudgetEXT)
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
        createInfo.vulkanApiVersion = deviceApiVersion;

        vk::Result res = static_cast<vk::Result>(vmaCreateAllocator(&createInfo, &impl->allocator));

        switch (res)
        {
        case vk::Result::eErrorFeatureNotPresent:
            throw vk::FeatureNotPresentError("vkq::MemoryAllocator::create");
        case vk::Result::eErrorInitializationFailed:
            throw vk::InitializationFailedError("vkq::MemoryAllocator::create");
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

    Buffer::Impl* MemoryAllocator::allocBufferHandle() const
    {
        std::lock_guard lock(impl->bufferHandleMutex);
        return impl->bufferHandles.alloc();
    }

    void MemoryAllocator::freeBufferHandle(Buffer::Impl* handle) const
    {
        std::lock_guard lock(impl->bufferHandleMutex);
        impl->bufferHandles.free(handle);
    }

    //////////////////////////////////
    // Memory Pools //////////////////
    //////////////////////////////////

    explicit MemoryPool::MemoryPool(MemoryAllocator allocator, VmaPool pool)
        : allocator_(allocator), pool_(pool)
    {
    }

    MemoryPool MemoryPool::create(const MemoryAllocator& allocator, MemoryPoolAlgorithm algorithm, uint32_t memoryTypeIndex, vk::DeviceSize blockSize = 0, uint32_t minBlockCount = 0, uint32_t maxBlockCount = 0)
    {
        VmaPoolCreateFlags flags = 0;
        if (algorithm == MemoryPoolAlgorithm::eBuddy)
            flags |= VMA_POOL_CREATE_BUDDY_ALGORITHM_BIT;

        VmaPoolCreateInfo createInfo{};
        createInfo.flags = flags;
        createInfo.blockSize = blockSize;
        createInfo.minBlockCount = minBlockCount;
        createInfo.maxBlockCount = maxBlockCount;
        createInfo.memoryTypeIndex = memoryTypeIndex;

        VmaPool pool;
        vk::Result res = static_cast<vk::Result>(vmaCreatePool(allocator.vmaAllocator(), &createInfo, &pool));
        vk::throwResultException(res, "vkq::MemoryPool::create");

        return MemoryPool{allocator, pool};
    }

    void MemoryPool::destroy()
    {
        vmaDestroyPool(allocator_.vmaAllocator(), pool_);

        allocator_ = {};
        pool_ = {};
    }

    ///////////////////////////////
    // Linear Memory Pool /////////
    ///////////////////////////////

    explicit LinearMemoryPool::LinearMemoryPool(MemoryAllocator allocator, VmaPool pool)
        : allocator_(allocator), pool_(pool)
    {
    }

    LinearMemoryPool LinearMemoryPool::create(const MemoryAllocator& allocator, uint32_t memoryTypeIndex, vk::DeviceSize size)
    {
        VmaPoolCreateFlags flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;

        VmaPoolCreateInfo createInfo{};
        createInfo.flags = flags;
        createInfo.blockSize = size;
        createInfo.minBlockCount = 1;
        createInfo.maxBlockCount = 1;
        createInfo.memoryTypeIndex = memoryTypeIndex;

        VmaPool pool;
        vk::Result res = static_cast<vk::Result>(vmaCreatePool(allocator.vmaAllocator(), &createInfo, &pool));
        vk::throwResultException(res, "vkq::MemoryPool::create");

        return LinearMemoryPool{allocator, pool};
    }

    void LinearMemoryPool::destroy()
    {
        vmaDestroyPool(allocator_.vmaAllocator(), pool_);

        allocator_ = {};
        pool_ = {};
    }

} // namespace vkq