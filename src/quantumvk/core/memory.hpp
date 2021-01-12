#pragma once

#include "../base/vk.hpp"
#include "../base/vma.hpp"

#include "device.hpp"

#include <utility>

namespace vkq
{
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
} // namespace vkq

namespace vkq
{

    class MemoryAllocator
    {
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
        struct Impl
        {
            Device device;
            VmaAllocator allocator;
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
        static MemoryPool create(const MemoryAllocator& allocator, uint32_t memoryTypeIndex, vk::DeviceSize blockSize = 0, uint32_t minBlockCount = 0, uint32_t maxBlockCount = 0);
        static MemoryPool createBuddy(const MemoryAllocator& allocator, uint32_t memoryTypeIndex, vk::DeviceSize blockSize = 0, uint32_t minBlockCount = 0, uint32_t maxBlockCount = 0);
        static MemoryPool createLinear(const MemoryAllocator& allocator, uint32_t memoryTypeIndex, vk::DeviceSize size);

        void destroy();

        MemoryAllocator allocator() const { return allocator_; }

        VmaPool vmaPool() const { return pool_; }
        VmaPool vmaHandle() const { return pool_; }
        operator VmaPool() const { return pool_; }

    private:
        MemoryAllocator allocator_;
        VmaPool pool_;
    };

    // enum class AllocationFlagBits
    // {
    //     eDedicatedMemory = 0x0001,
    //     eNeverAllocate = 0x0002,
    //     eMapped = 0x0004
    // };

    // using AllocationFlags = vk::Flags<AllocationFlagBits>;

    // AllocationFlags operator|(AllocationFlagBits bit0, AllocationFlagBits bit1)
    // {
    //     return AllocationFlags(bit0) | bit1;
    // }

    // AllocationFlags operator~(AllocationFlagBits bits)
    // {
    //     return ~(AllocationFlags(bits));
    // }

    // enum class AllocationStrategy
    // {
    //     eBestFit = 0x0001,
    //     eWorstFit = 0x0002,
    //     eFirstFit = 0x0004,
    //     eMinMemory = 0x0008,
    //     eMinTime = 0x0010,
    //     eMinFragmentation = 0x0020
    // };

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
        static Buffer create(const MemoryAllocator, const vk::BufferCreateInfo& createInfo, vk::MemoryPropertyFlags requiredFlags, vk::MemoryPropertyFlags preferedFlags, uint32_t allowedMemoryTypesBitMask);

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

        uint32_t memoryTypeIndex() const { return memoryTypeIndex_; }
        vk::MemoryType memoryTypeProperties() const { return allocator_.memoryTypeProperties(memoryTypeIndex_); }
        bool memoryHasPropertyFlags(vk::MemoryPropertyFlags flags) const { return uint32_t(allocator_.memoryTypeProperties(memoryTypeIndex_).propertyFlags & flags); }

    private:
        MemoryAllocator allocator_;
        VmaAllocation allocation_;
        vk::Buffer buffer_;

        vk::DeviceSize size_;
        vk::BufferUsageFlags usage_;
        uint32_t memoryTypeIndex_;
    };

} // namespace vkq
