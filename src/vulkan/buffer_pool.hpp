#pragma once

#include "vulkan_headers.hpp"
#include "utils/intrusive.hpp"
#include <vector>
#include <algorithm>

namespace Vulkan
{
	//Forward declare device
	class Device;
	//Forward declare buffer
	class Buffer;

	//A suballocation into a BufferBlock
	struct BufferBlockAllocation
	{
		uint8_t* host;
		VkDeviceSize offset;
		VkDeviceSize padded_size;
	};

	//A buffer visible on both the cpu and gpu that can be suballocated from
	struct BufferBlock
	{
		~BufferBlock();
		Util::IntrusivePtr<Buffer> gpu;
		Util::IntrusivePtr<Buffer> cpu;
		VkDeviceSize offset = 0;
		VkDeviceSize alignment = 0;
		VkDeviceSize size = 0;
		VkDeviceSize spill_size = 0;
		uint8_t* mapped = nullptr;

		//Suballocate from buffer block
		BufferBlockAllocation Allocate(VkDeviceSize allocate_size)
		{
			auto aligned_offset = (offset + alignment - 1) & ~(alignment - 1);
			if (aligned_offset + allocate_size <= size)
			{
				auto* ret = mapped + aligned_offset;
				offset = aligned_offset + allocate_size;

				VkDeviceSize padded_size = std::max(allocate_size, spill_size);
				padded_size = std::min(padded_size, size - aligned_offset);

				return { ret, aligned_offset, padded_size };
			}
			else
				return { nullptr, 0, 0 };
		}
	};

	//A Pool of BufferBlocks
	class BufferPool
	{
	public:

		~BufferPool();
		void init(Device* device, VkDeviceSize block_size, VkDeviceSize alignment, VkBufferUsageFlags usage, bool need_device_local);
		void reset();

		// Used for allocating UBOs, where we want to specify a fixed size for range,
		// and we need to make sure we don't allocate beyond the block.
		void set_spill_region_size(VkDeviceSize spill_size);

		VkDeviceSize get_block_size() const
		{
			return block_size;
		}

		//Request a new block of a certain size
		BufferBlock request_block(VkDeviceSize minimum_size);
		//Recycle an old unused block
		void recycle_block(BufferBlock&& block);

	private:
		Device* device = nullptr;
		VkDeviceSize block_size = 0;
		VkDeviceSize alignment = 0;
		VkDeviceSize spill_size = 0;
		VkBufferUsageFlags usage = 0;
		std::vector<BufferBlock> blocks;
		BufferBlock allocate_block(VkDeviceSize size);
		bool need_device_local = false;
	};
}