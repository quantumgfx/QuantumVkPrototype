#pragma once

#include "../base/vk.hpp"
#include "../base/vma.hpp"

#include "device.hpp"

#include <utility>

namespace vkq
{

    class MemoryAllocator
    {
    public:
        static MemoryAllocator create(const Device& device);

        void destroy();

        VmaAllocator vmaAllocator() const { return allocator; }
        VmaAllocator vmaHandle() const { return allocator; }
        operator VmaAllocator() const { return allocator; }

    private:
        VmaAllocator allocator;
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

        MemoryAllocator getAllocator() const { return allocator; }

        VmaPool vmaPool() const { return pool; }
        VmaPool vmaHandle() const { return pool; }
        operator VmaPool() const { return pool; }

    private:
        MemoryAllocator allocator;
        VmaPool pool;
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

    // class Buffer
    // {
    // public:
    //     static Buffer create(const MemoryPool& pool, const vk::BufferCreateInfo& createInfo, AllocationFlags flags = {}, AllocationStrategy stategy = {});
    //     static Buffer create(const MemoryPool& pool, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::SharingMode sharingMode, vk::ArrayProxyNoTemporaries<const uint32_t> const& queueFamilyIndicies);

    //     void* mapMemory();

    //     void unmapMemory();

    //     void invalidateMemory();

    //     void flushMemory();

    // private:
    //     MemoryAllocator allocator;
    //     vk::Buffer buffer;
    //     VmaAllocation allocation;
    // };

} // namespace vkq
