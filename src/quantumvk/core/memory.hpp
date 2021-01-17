#pragma once

#include "../base/common.hpp"
#include "../base/vk.hpp"
#include "../base/vma.hpp"

#include "device.hpp"

#include <mutex>
#include <utility>

namespace vkq
{
    //////////////////////////////////
    // Map Memory Access /////////////
    //////////////////////////////////

    enum class MapMemoryAccessFlagBits
    {
        eRead = 0x00000001,
        eWrite = 0x00000002,
        eReadWrite = eRead | eWrite
    };

    using MapMemoryAccessFlags = vk::Flags<MapMemoryAccessFlagBits>;

    MapMemoryAccessFlags operator|(MapMemoryAccessFlagBits bit0, MapMemoryAccessFlagBits bit1)
    {
        return MapMemoryAccessFlags(bit0) | bit1;
    }

    MapMemoryAccessFlags operator~(MapMemoryAccessFlagBits bits)
    {
        return ~(MapMemoryAccessFlags(bits));
    }

    /////////////////////////////////
    // Linear ALlocations ///////////
    /////////////////////////////////

    enum class LinearAllocationFlagBits
    {
        eMapped = 0x0001,
        eUpperAddress = 0x0002
    };

    using LinearAllocationFlags = vk::Flags<LinearAllocationFlagBits>;

    LinearAllocationFlags operator|(LinearAllocationFlagBits bit0, LinearAllocationFlagBits bit1)
    {
        return LinearAllocationFlags(bit0) | bit1;
    }

    LinearAllocationFlags operator~(LinearAllocationFlagBits bits)
    {
        return ~(LinearAllocationFlags(bits));
    }

    ///////////////////////////////////
    // Pool Allocation ////////////////
    ///////////////////////////////////

    enum class PoolAllocationFlagBits
    {
        eMapped = 0x0001,
        eNeverAllocate = 0x0002,
        eWithinBudget = 0x0004,
    };

    using PoolAllocationFlags = vk::Flags<PoolAllocationFlagBits>;

    PoolAllocationFlags operator|(PoolAllocationFlagBits bit0, PoolAllocationFlagBits bit1)
    {
        return PoolAllocationFlags(bit0) | bit1;
    }

    PoolAllocationFlags operator~(PoolAllocationFlagBits bits)
    {
        return ~(PoolAllocationFlags(bits));
    }

    enum class MemoryPoolAlgorithm
    {
        eDefault = 0,
        eBuddy = 1,
    };

    /////////////////////////////////
    // General Allocation ///////////
    /////////////////////////////////

    enum class AllocationStrategy
    {
        eStrategyMinMemory = 0,
        eStrategyMinTime = 1,
        eStrategyMinFragmentation = 2,
    };

    enum class AllocationFlagBits
    {
        eDedicatedMemory = 0x0001,
        eNeverAllocate = 0x0002,
        eMapped = 0x0004,
        eWithinBudget = 0x0008,
    };

    using AllocationFlags = vk::Flags<AllocationFlagBits>;

    AllocationFlags operator|(AllocationFlagBits bit0, AllocationFlagBits bit1)
    {
        return AllocationFlags(bit0) | bit1;
    }

    AllocationFlags operator~(AllocationFlagBits bits)
    {
        return ~(AllocationFlags(bits));
    }
} // namespace vkq

namespace vkq
{

    class MemoryAllocator;
    class MemoryPool;
    class LinearMemoryPool;

    /**
     * @brief Abstracts vk::Buffer and the associated VmaAllocation object. Provides functions to 
     * retrieve the buffer's properties as well as map its memory if it is host visible.
     * 
     */
    class Buffer
    {
    public:
        Buffer() = default;
        ~Buffer() = default;

    public:
        /**
         * @brief Creates a buffer using createInfo, and allocates memory for it from the specifed LinearMemoryPool.
         * 
         * @param linearPool Linear Memory Pool to allocate from.
         * @param createInfo Info used to create buffer.
         * @param allocFlags Flags used to specify additional allocation settings.
         * @return Newly created and allocated buffer.  
         */
        static Buffer create(const LinearMemoryPool& linearPool, const vk::BufferCreateInfo& createInfo, LinearAllocationFlags allocFlags = {});

        /**
         * @brief Creates a buffer using createInfo, and allocates memory for it from the specifed MemoryPool.
         * 
         * @param pool Pool to allocate memory from.
         * @param createInfo Info used to create buffer.
         * @param allocFlags Flags used to specify additional allocation settings.
         * @param strategy Allocation strategy.
         * @return Newly created and allocated buffer.  
         */
        static Buffer create(const MemoryPool& pool, const vk::BufferCreateInfo& createInfo, PoolAllocationFlags allocFlags = {}, AllocationStrategy strategy = AllocationStrategy::eStrategyMinMemory);

        /**
         * @brief Creates a buffer using createInfo, and allocates memory for it as specified by the other parameters.
         * 
         * @param allocator Allocator to allocate memory from.
         * @param createInfo Info used to create buffer.
         * @param requiredMemFlags Property flags that the memory type the buffer is allocated from must have.
         * @param preferredMemFlags Property flags that the memory type the buffer is allocated from is preffered (but not guaranteed) to have.
         * @param allocFlags Flags used to specify additional allocation settings.
         * @param strategy Allocation strategy.
         * @param memoryTypeBits Bitmask with bits set for each allowed memory type (0 defaults to UINT32_MAX).
         * @return Newly created and allocated buffer. 
         */
        static Buffer create(const MemoryAllocator& allocator, const vk::BufferCreateInfo& createInfo, vk::MemoryPropertyFlags requiredMemFlags, vk::MemoryPropertyFlags preferredMemFlags, AllocationFlags allocFlags = {}, AllocationStrategy strategy = AllocationStrategy::eStrategyMinMemory, uint32_t memoryTypeBits = UINT32_MAX);

        /**
         * @brief Destroys and deallocates the buffer. The resource must not be in use by any pending operations or commands.
         * The buffer must not be mapped when it is destroyed.
         * 
         */
        void destroy();

        /**
         * @brief Maps buffer memory. Pointer to memory can be retrieved using vkq::Buffer::hostMemory().
         * Throws vk::OutOfHostMemoryError(), vk::OutOfDeviceMemoryError(), or vk::MapMemoryFailedError()
         * if underlying vkMapMemory or vkInvalidateMappedMemoryRanges function fails.
         * 
         * @param flags Describes how the mapped memory will be accessed. If the memory is to be read from,
         * this will call vmaInvalidateAllocation if the memory is not host coherent.
         */
        void mapMemory(MapMemoryAccessFlags flags);

        /**
         * @brief Unmaps buffer memory. Calls to vkq::Buffer::hostMemory() are undefined until mapMemory() is called again.
         * Throws vk::OutOfHostMemoryError(), or vk::OutOfDeviceMemoryError()
         * if underlying vkFlushMappedMemoryRanges function fails.
         * 
         * @param flags Describes how the mapped memory was be accessed. If the memory was written to,
         * this will call vmaFlushAllocation if the memory is not host coherent.
         */
        void unmapMemory(MapMemoryAccessFlags flags);

        /**
         * @brief Returns the host pointer to the buffer. Only defined if called between mapMemory() and unmapMemory().
         * 
         * @return void* pointing to buffer host memory.
         */
        void* hostMemory();

        /**
         * @brief Returns the index of the memory type this buffer is allocated from.
         * 
         * @return uint32_t 
         */
        uint32_t memoryTypeIndex() const;

        /**
         * @brief Returns the properties of the memory this buffer is allocated from.
         * 
         * @return vk::MemoryType 
         */
        vk::MemoryType memoryTypeProperties() const;

        /**
         * @brief Returns whether the memory this buffer is allocated from has certain properties.
         * 
         * @param flags
         * @return true
         * @return false 
         */
        bool memoryHasPropertyFlags(vk::MemoryPropertyFlags flags) const;

        /**
         * @brief Retrieves size passed into vk::BufferCreateInfo when this buffer was created.
         * 
         * @return Size of buffer in bytes.
         */
        vk::DeviceSize size();

        /**
         * @brief Retrieves usage passed info vk::BufferCreateInfo when this buffer was created.
         * 
         * @return Usage of the buffer.
         */
        vk::BufferUsageFlags usage();

        Device device() const;
        MemoryAllocator allocator() const;

        VmaAllocation vmaAllocation() const;
        vk::Buffer vkBuffer() const;
        vk::Buffer vkHandle() const;
        operator vk::Buffer() const;

    public:
        struct Impl
        {
            MemoryAllocator allocator;
            VmaAllocation allocation;
            vk::Buffer buffer;

            vk::DeviceSize size;
            vk::BufferUsageFlags usage;

            void* hostMemory;
            uint32_t memoryTypeIndex;
        };

    private:
        explicit Buffer(Impl* impl);

        Impl* impl;
    };

    /**
     * @brief Abstracts vk::Buffer and the associated VmaAllocation object. Provides functions to 
     * retrieve the buffer's properties as well as map its memory if it is host visible.
     * 
     */
    class Image
    {
    public:
        Image() = default;
        ~Image() = default;

    public:
        /**
         * @brief Creates an image using createInfo, and allocates memory for it from the specifed LinearMemoryPool.
         * 
         * @param linearPool Linear Memory Pool to allocate from.
         * @param createInfo Info used to create image.
         * @param allocFlags Flags used to specify additional allocation settings.
         * @return Newly created and allocated image.  
         */
        static Image create(const LinearMemoryPool& linearPool, const vk::ImageCreateInfo& createInfo, LinearAllocationFlags allocFlags = {});

        /**
         * @brief Creates an image using createInfo, and allocates memory for it from the specifed MemoryPool.
         * 
         * @param pool Pool to allocate memory from.
         * @param createInfo Info used to create image.
         * @param allocFlags Flags used to specify additional allocation settings.
         * @param strategy Allocation strategy.
         * @return Newly created and allocated image.  
         */
        static Image create(const MemoryPool& pool, const vk::ImageCreateInfo& createInfo, PoolAllocationFlags allocFlags = {}, AllocationStrategy strategy = AllocationStrategy::eStrategyMinMemory);

        /**
         * @brief Creates an image using createInfo, and allocates memory for it as specified by the other parameters.
         * 
         * @param allocator Allocator to allocate memory from.
         * @param createInfo Info used to create image.
         * @param requiredMemFlags Property flags that the memory type the image is allocated from must have.
         * @param preferredMemFlags Property flags that the memory type the image is allocated from is preffered (but not guaranteed) to have.
         * @param allocFlags Flags used to specify additional allocation settings.
         * @param strategy Allocation strategy.
         * @param memoryTypeBits Bitmask with bits set for each allowed memory type (0 defaults to UINT32_MAX).
         * @return Newly created and allocated image. 
         */
        static Image create(const MemoryAllocator& allocator, const vk::ImageCreateInfo& createInfo, vk::MemoryPropertyFlags requiredMemFlags, vk::MemoryPropertyFlags preferredMemFlags, AllocationFlags allocFlags = {}, AllocationStrategy strategy = AllocationStrategy::eStrategyMinMemory, uint32_t memoryTypeBits = UINT32_MAX);

        /**
         * @brief Destroys and deallocates the image. The resource must not be in use by any pending operations or commands.
         * The image must not be mapped when it is destroyed.
         * 
         */
        void destroy();

        /**
         * @brief Maps image memory. Pointer to memory can be retrieved using vkq::Image::hostMemory().
         * Throws vk::OutOfHostMemoryError(), vk::OutOfDeviceMemoryError(), or vk::MapMemoryFailedError()
         * if underlying vkMapMemory or vkInvalidateMappedMemoryRanges function fails.
         * 
         * @param flags Describes how the mapped memory will be accessed. If the memory is to be read from,
         * this will call vmaInvalidateAllocation if the memory is not host coherent.
         */
        void mapMemory(MapMemoryAccessFlags flags);

        /**
         * @brief Unmaps image memory. Calls to vkq::Image::hostMemory() are undefined until mapMemory() is called again.
         * Throws vk::OutOfHostMemoryError(), or vk::OutOfDeviceMemoryError()
         * if underlying vkFlushMappedMemoryRanges function fails.
         * 
         * @param flags Describes how the mapped memory was be accessed. If the memory was written to,
         * this will call vmaFlushAllocation if the memory is not host coherent.
         */
        void unmapMemory(MapMemoryAccessFlags flags);

        /**
         * @brief Returns the host pointer to the image. Only defined if called between mapMemory() and unmapMemory().
         * 
         * @return void* pointing to image host memory.
         */
        void* hostMemory();

        /**
         * @brief Returns the index of the memory type this image is allocated from.
         * 
         * @return uint32_t 
         */
        uint32_t memoryTypeIndex() const;

        /**
         * @brief Returns the properties of the memory this buffer is allocated from.
         * 
         * @return vk::MemoryType 
         */
        vk::MemoryType memoryTypeProperties() const;

        /**
         * @brief Returns whether the memory this image is allocated from has certain properties.
         * 
         * @param flags
         * @return true
         * @return false 
         */
        bool memoryHasPropertyFlags(vk::MemoryPropertyFlags flags) const;

        vk::ImageType imageType() const;

        vk::Format format() const;

        vk::Extent3D extent() const;

        uint32_t mipLevels() const;

        uint32_t arrayLayers() const;

        vk::SampleCountFlagBits samples() const;

        vk::ImageTiling tiling() const;

        vk::ImageUsageFlags usage() const;

        Device device() const;
        MemoryAllocator allocator() const;

        VmaAllocation vmaAllocation() const;
        vk::Image vkImage() const;
        vk::Image vkHandle() const;
        operator vk::Image() const;

    public:
        struct Impl
        {
            MemoryAllocator allocator;
            VmaAllocation allocation;
            vk::Image image;

            vk::ImageType imageType;
            vk::Format format;
            vk::Extent3D extent;
            uint32_t mipLevels;
            uint32_t arrayLayers;
            vk::SampleCountFlagBits samples;
            vk::ImageTiling tiling;
            vk::ImageUsageFlags usage;

            void* hostMemory;
            uint32_t memoryTypeIndex;
        };

    private:
        explicit Image(Impl* impl);

        Impl* impl;
    };

    /**
     * @brief Utility handle class to manage memory allocations for a device.
     * 
     */
    class MemoryAllocator
    {
        friend class Buffer;
        friend class Image;

    public:
        MemoryAllocator() = default;
        ~MemoryAllocator() = default;

    public:
        static MemoryAllocator create(const Device& device, vk::DeviceSize prefferedLargeHeapBlockSize = 256 * 1024 * 1024);

        void destroy();

        const vk::PhysicalDeviceMemoryProperties& memoryProperties() const { return device().memoryProperties(); }
        vk::MemoryType memoryTypeProperties(uint32_t memoryTypeIndex) const { return device().memoryTypeProperties(memoryTypeIndex); }
        vk::MemoryHeap memoryHeapProperties(uint32_t memoryHeapIndex) const { return device().memoryHeapProperties(memoryHeapIndex); }

        Device device() const;
        VmaAllocator vmaAllocator() const;
        VmaAllocator vmaHandle() const;
        operator VmaAllocator() const;

    private:
        Buffer::Impl* allocBufferHandle() const;
        void freeBufferHandle(Buffer::Impl* handle) const;

        Image::Impl* allocImageHandle() const;
        void freeImageHandle(Image::Impl* handle) const;

    private:
        struct Impl
        {
            Device device;
            VmaAllocator allocator;

            std::mutex bufferHandleMutex;
            ObjectPool<Buffer::Impl> bufferHandles;

            std::mutex imageHandleMutex;
            ObjectPool<Image::Impl> imageHandles;
        };

        explicit MemoryAllocator(Impl* impl);

        Impl* impl = nullptr;
    };

    class MemoryPool
    {
    public:
        MemoryPool() = default;
        ~MemoryPool() = default;

    public:
        static MemoryPool create(const MemoryAllocator& allocator, MemoryPoolAlgorithm algorithm, uint32_t memoryTypeIndex, vk::DeviceSize blockSize = 0, uint32_t minBlockCount = 0, uint32_t maxBlockCount = 0);

        void destroy();

        MemoryAllocator allocator() const { return allocator_; }

        VmaPool vmaPool() const { return pool_; }
        VmaPool vmaHandle() const { return pool_; }
        operator VmaPool() const { return pool_; }

    private:
        explicit MemoryPool(MemoryAllocator allocator, VmaPool pool);

        MemoryAllocator allocator_;
        VmaPool pool_;
    };

    class LinearMemoryPool
    {
    public:
        LinearMemoryPool() = default;
        ~LinearMemoryPool() = default;

    public:
        static LinearMemoryPool create(const MemoryAllocator& allocator, uint32_t memoryTypeIndex, vk::DeviceSize size);

        void destroy();

        MemoryAllocator allocator() const { allocator_; }

        VmaPool vmaPool() const { return pool_; }
        VmaPool vmaHandle() const { return pool_; }
        operator VmaPool() const { return pool_; }

    private:
        explicit LinearMemoryPool(MemoryAllocator allocator, VmaPool pool);

        MemoryAllocator allocator_;
        VmaPool pool_;
    };
} // namespace vkq
