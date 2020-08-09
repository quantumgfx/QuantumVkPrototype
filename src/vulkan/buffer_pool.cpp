#include "buffer_pool.hpp"
#include "device.hpp"
#include <utility>

using namespace std;

namespace Vulkan
{
	void BufferPool::Init(Device* device_, VkDeviceSize block_size_,
		VkDeviceSize alignment_, VkBufferUsageFlags usage_,
		bool need_device_local_)
	{
		device = device_;
		block_size = block_size_;
		alignment = alignment_;
		usage = usage_;
		need_device_local = need_device_local_;
	}

	void BufferPool::SetSpillRegionSize(VkDeviceSize spill_size_)
	{
		spill_size = spill_size_;
	}

	BufferBlock::~BufferBlock()
	{
	}

	void BufferPool::Reset()
	{
		blocks.clear();
	}

	BufferBlock BufferPool::AllocateBlock(VkDeviceSize size)
	{
		// If needs device_local, domain device, if staging buffer, domain host, if index, vertex or uniform LinkedDeviceHost.
		BufferDomain ideal_domain = need_device_local ? BufferDomain::Device : ((usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0) ? BufferDomain::Host : BufferDomain::LinkedDeviceHost;

		VkBufferUsageFlags extra_usage = ideal_domain == BufferDomain::Device ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0;

		BufferBlock block;

		BufferCreateInfo info;
		info.domain = ideal_domain;
		info.size = size;
		info.usage = usage | extra_usage;

		block.gpu = device->CreateBuffer(info, nullptr);
		block.gpu->SetInternalSyncObject();

		if (device->AllocationHasMemoryPropertyFlags(block.gpu->GetAllocation(), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
		{
			VK_ASSERT(block.gpu->GetAllocation().persistantly_mapped);

			block.mapped = static_cast<uint8_t*>(device->MapHostBuffer(*block.cpu, MEMORY_ACCESS_WRITE_BIT));
			block.cpu = block.gpu;
		}
		else
		{
			// Fall back to host memory, and remember to sync to gpu on submission time using DMA queue. :)
			BufferCreateInfo cpu_info;
			cpu_info.domain = BufferDomain::Host;
			cpu_info.size = size;
			cpu_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

			block.cpu = device->CreateBuffer(cpu_info, nullptr);

			VK_ASSERT(block.cpu->GetAllocation().persistantly_mapped);

			block.cpu->SetInternalSyncObject();
			block.mapped = static_cast<uint8_t*>(device->MapHostBuffer(*block.cpu, MEMORY_ACCESS_WRITE_BIT));
		}

		block.offset = 0;
		block.alignment = alignment;
		block.size = size;
		block.spill_size = spill_size;
		return block;
	}

	BufferBlock BufferPool::RequestBlock(VkDeviceSize minimum_size)
	{
		if ((minimum_size > block_size) || blocks.empty())
		{ // If the request is too large or the pool empty, allocate a new block.
			return AllocateBlock(max(block_size, minimum_size));
		}
		else
		{ // Else get the last block in the pool
			auto back = move(blocks.back());
			blocks.pop_back();

			back.mapped = static_cast<uint8_t*>(device->MapHostBuffer(*back.cpu, MEMORY_ACCESS_WRITE_BIT));
			back.offset = 0;
			return back;
		}
	}

	void BufferPool::RecycleBlock(BufferBlock&& block)
	{
		VK_ASSERT(block.size == block_size);
		blocks.push_back(move(block));
	}

	BufferPool::~BufferPool()
	{
		VK_ASSERT(blocks.empty());
	}

}