#include "device.hpp"
#include "images/format.hpp"
#include "misc/type_to_string.hpp"

#include "quantumvk/utils/timer.hpp"
#include <algorithm>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

#ifdef QM_VULKAN_MT
#include "quantumvk/threading/thread_id.hpp"
static unsigned get_thread_index()
{
	return Vulkan::get_current_thread_index();
}
#define LOCK() std::lock_guard<std::mutex> holder__{lock.lock}
#define DRAIN_FRAME_LOCK() \
	std::unique_lock<std::mutex> holder__{lock.lock}; \
	lock.cond.wait(holder__, [&]() { \
		return lock.counter == 0; \
	})
#else
#define LOCK() ((void)0)
#define DRAIN_FRAME_LOCK() VK_ASSERT(lock.counter == 0)
static unsigned get_thread_index()
{
	return 0;
}
#endif

using namespace std;
using namespace Util;

namespace Vulkan
{
	Device::Device()
		: framebuffer_allocator(this)
		, transient_allocator(this)
		, physical_allocator(this)
	{
#ifdef QM_VULKAN_MT
		cookie.store(0);
#endif

	}

	Semaphore Device::RequestLegacySemaphore()
	{
		LOCK();
		//Gets a cleared semaphore
		auto semaphore = managers.semaphore.RequestClearedSemaphore();
		//allocates the pointer from the object pool
		Semaphore ptr(handle_pool.semaphores.allocate(this, semaphore, false));
		//returns the ptr
		return ptr;
	}

	Semaphore Device::RequestExternalSemaphore(VkSemaphore semaphore, bool signalled)
	{
		LOCK();
		VK_ASSERT(semaphore);
		//Allocates the handle from the object pool
		Semaphore ptr(handle_pool.semaphores.allocate(this, semaphore, signalled));
		return ptr;
	}

	void Device::AddWaitSemaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush)
	{
		LOCK();
		AddWaitSemaphoreNolock(type, semaphore, stages, flush);
	}

	void Device::AddWaitSemaphoreNolock(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush)
	{
		VK_ASSERT(stages != 0);
		if (flush)
			FlushFrame(type);
		auto& data = GetQueueData(type);

#ifdef VULKAN_DEBUG
		for (auto& sem : data.wait_semaphores)
			VK_ASSERT(sem.Get() != semaphore.Get());
#endif

		semaphore->SignalPendingWaits();
		data.wait_semaphores.push_back(semaphore);
		data.wait_stages.push_back(stages);
		data.need_fence = true;

		// Sanity check.
		VK_ASSERT(data.wait_semaphores.size() < 16 * 1024);
	}

	LinearHostImageHandle Device::CreateLinearHostImage(const LinearHostImageCreateInfo& info)
	{
		if ((info.usage & ~VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
			return LinearHostImageHandle(nullptr);

		ImageCreateInfo create_info;
		create_info.width = info.width;
		create_info.height = info.height;
		create_info.domain = (info.flags & LINEAR_HOST_IMAGE_HOST_CACHED_BIT) != 0 ? ImageDomain::LinearHostCached : ImageDomain::LinearHost;
		create_info.levels = 1;
		create_info.layers = 1;
		create_info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
		create_info.format = info.format;
		create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		create_info.usage = info.usage;
		create_info.type = VK_IMAGE_TYPE_2D;

		if ((info.flags & LINEAR_HOST_IMAGE_REQUIRE_LINEAR_FILTER_BIT) != 0)
			create_info.misc |= IMAGE_MISC_VERIFY_FORMAT_FEATURE_SAMPLED_LINEAR_FILTER_BIT;
		if ((info.flags & LINEAR_HOST_IMAGE_IGNORE_DEVICE_LOCAL_BIT) != 0)
			create_info.misc |= IMAGE_MISC_LINEAR_IMAGE_IGNORE_DEVICE_LOCAL_BIT;

		BufferHandle cpu_image;
		auto gpu_image = CreateImage(create_info, RESOURCE_EXCLUSIVE_GENERIC);
		if (!gpu_image)
		{
			// Fall-back to staging buffer.
			create_info.domain = ImageDomain::Physical;
			create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			create_info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			gpu_image = CreateImage(create_info, RESOURCE_CONCURRENT_GENERIC | RESOURCE_CONCURRENT_ASYNC_TRANSFER);
			if (!gpu_image)
				return LinearHostImageHandle(nullptr);

			BufferCreateInfo buffer;
			buffer.domain = (info.flags & LINEAR_HOST_IMAGE_HOST_CACHED_BIT) != 0 ? BufferDomain::CachedHost : BufferDomain::Host;
			buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			buffer.size = info.width * info.height * TextureFormatLayout::format_block_size(info.format, FormatToAspectMask(info.format));
			cpu_image = CreateBuffer(buffer);
			if (!cpu_image)
				return LinearHostImageHandle(nullptr);
		}
		else
			gpu_image->SetLayout(Layout::General);

		return LinearHostImageHandle(handle_pool.linear_images.allocate(this, move(gpu_image), move(cpu_image), info.stages));
	}

	void* Device::MapLinearHostImage(const LinearHostImage& image, MemoryAccessFlags access)
	{
		void* host = managers.memory.MapMemory(image.GetHostVisibleAllocation(), access);
		return host;
	}

	void Device::UnmapLinearHostImageAndSync(const LinearHostImage& image, MemoryAccessFlags access)
	{
		managers.memory.UnmapMemory(image.GetHostVisibleAllocation(), access);
		if (image.NeedStagingCopy())
		{
			// Kinda icky fallback, shouldn't really be used on discrete cards.
			auto cmd = RequestCommandBuffer(CommandBuffer::Type::AsyncTransfer);
			cmd->ImageBarrier(image.GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
			cmd->CopyBufferToImage(image.GetImage(), image.GetHostVisibleBuffer(),
				0, {},
				{ image.GetImage().GetWidth(), image.GetImage().GetHeight(), 1 },
				0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 });

			// Don't care about dstAccessMask, semaphore takes care of everything.
			cmd->ImageBarrier(image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);

			Semaphore sem;
			Submit(cmd, nullptr, 1, &sem);

			// The queue type is an assumption. Should add some parameter for that.
			AddWaitSemaphore(CommandBuffer::Type::Generic, sem, image.GetUsedPipelineStages(), true);
		}
	}
	
	void* Device::MapLinearHostImage(const Image& image, MemoryAccessFlags access)
	{
		VK_ASSERT(image.GetCreateInfo().domain == ImageDomain::LinearHost || image.GetCreateInfo().domain == ImageDomain::LinearHostCached);
		void* host = managers.memory.MapMemory(image.GetAllocation(), access);
		return host;
	}
	
	void Device::UnmapLinearHostImage(const Image& image, MemoryAccessFlags access)
	{
		VK_ASSERT(image.GetCreateInfo().domain == ImageDomain::LinearHost || image.GetCreateInfo().domain == ImageDomain::LinearHostCached);
		managers.memory.UnmapMemory(image.GetAllocation(), access);
	}

	void* Device::MapHostBuffer(const Buffer& buffer, MemoryAccessFlags access)
	{
		void* host = managers.memory.MapMemory(buffer.GetAllocation(), access);
		return host;
	}

	void Device::UnmapHostBuffer(const Buffer& buffer, MemoryAccessFlags access)
	{
		managers.memory.UnmapMemory(buffer.GetAllocation(), access);
	}

	void Device::InitWorkarounds()
	{
		workarounds = {};

#ifdef __APPLE__
		// Events are not supported in MoltenVK.
		workarounds.emulate_event_as_pipeline_barrier = true;
		LOGW("Emulating events as pipeline barriers on Metal emulation.\n");
#else
		if (gpu_props.vendorID == VENDOR_ID_NVIDIA &&
#ifdef _WIN32
			VK_VERSION_MAJOR(gpu_props.driverVersion) < 417)
#else
			VK_VERSION_MAJOR(gpu_props.driverVersion) < 415)
#endif
		{
			workarounds.force_store_in_render_pass = true;
			QM_LOG_WARN("Detected workaround for render pass STORE_OP_STORE.\n");
		}

		if (gpu_props.vendorID == VENDOR_ID_QCOM)
		{
			// Apparently, we need to use STORE_OP_STORE in all render passes no matter what ...
			workarounds.force_store_in_render_pass = true;
			workarounds.broken_color_write_mask = true;
			QM_LOG_WARN("Detected workaround for render pass STORE_OP_STORE.\n");
			QM_LOG_WARN("Detected workaround for broken color write masks.\n");
		}

		// UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL stalls, so need to acquire async.
		if (gpu_props.vendorID == VENDOR_ID_ARM)
		{
			QM_LOG_WARN("Workaround applied: Acquiring WSI images early on Mali.\n");
			QM_LOG_WARN("Workaround applied: Emulating events as pipeline barriers.\n");
			QM_LOG_WARN("Workaround applied: Optimize ALL_GRAPHICS_BIT barriers.\n");

			// All performance related workarounds.
			workarounds.wsi_acquire_barrier_is_expensive = true;
			workarounds.emulate_event_as_pipeline_barrier = true;
			workarounds.optimize_all_graphics_barrier = true;
		}
#endif
	}

	bool Device::InitPipelineCache(const uint8_t* initial_cache_data, size_t initial_cache_size) 
	{
		static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);

		VkPipelineCacheCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
		if (!initial_cache_data || size == 0)
		{
			QM_LOG_INFO("Creating a fresh pipeline cache.\n");
		}
		else if (memcmp(initial_cache_data, gpu_props.pipelineCacheUUID, uuid_size) != 0)
		{
			QM_LOG_INFO("Pipeline cache UUID changed.\n");
		}
		else
		{
			info.initialDataSize = size;
			info.pInitialData = initial_cache_data;
			QM_LOG_INFO("Initializing pipeline cache.\n");
		}

		if (pipeline_cache != VK_NULL_HANDLE)
			table->vkDestroyPipelineCache(device, pipeline_cache, nullptr);
		pipeline_cache = VK_NULL_HANDLE;
		return table->vkCreatePipelineCache(device, &info, nullptr, &pipeline_cache) == VK_SUCCESS;
	}

	std::vector<uint8_t> Vulkan::Device::GetPipelineCacheData(size_t override_max_size)
	{
		size_t max_size;
		if (table->vkGetPipelineCacheData(device, pipeline_cache, &max_size, nullptr) != VK_SUCCESS)
		{
			QM_LOG_ERROR("Failed to get pipeline cache size.\n");
		}

		if (override_max_size != 0)
		{
			if (max_size > override_max_size)
			{
				QM_LOG_ERROR("Clamping max pipeline cache size");
				max_size = override_max_size;
			}
		}

		std::vector<uint8_t> data;
		data.resize(max_size);

		if (table->vkGetPipelineCacheData(device, pipeline_cache, &max_size, data.data()) != VK_SUCCESS)
		{
			QM_LOG_ERROR("Failed to get pipeline cache data.\n");
		}

		return data;
	}

	void Device::SetContext(Context* context_, uint8_t* initial_cache_data, size_t initial_cache_size)
	{
		context = context_;
		table = &context_->GetDeviceTable();
		ext = &context_->GetEnabledDeviceExtensions();
		feat = context_->GetSupportedDeviceFeatures();

#ifdef QM_VULKAN_MT
		register_thread_index(0);
#endif
		instance = context_->GetInstance();
		gpu = context_->GetGPU();
		device = context_->GetDevice();
		num_thread_indices = context_->GetNumThreadIndices();

		graphics_queue_family_index = context_->GetGraphicsQueueFamily();
		graphics_queue = context_->GetGraphicsQueue();
		compute_queue_family_index = context_->GetComputeQueueFamily();
		compute_queue = context_->GetComputeQueue();
		transfer_queue_family_index = context_->GetTransferQueueFamily();
		transfer_queue = context_->GetTransferQueue();
		timestamp_valid_bits = context_->GetTimestampValidBits();

		mem_props = context_->GetMemProps();
		gpu_props = context_->GetGPUProps();

		InitWorkarounds();

		InitStockSamplers();
		InitTimelineSemaphores();

#ifdef ANDROID
		InitFrameContexts(3); // Android needs a bit more ... ;)
#else
		InitFrameContexts(2); // By default, regular double buffer between CPU and GPU.
#endif

		managers.memory.Init(this);
		managers.semaphore.Init(this);
		managers.fence.Init(this);
		managers.event.Init(this);
		managers.vbo.Init(this, 4 * 1024, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ImplementationQuirks::get().staging_need_device_local);
		managers.ibo.Init(this, 4 * 1024, 16, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ImplementationQuirks::get().staging_need_device_local);
		managers.ubo.Init(this, 256 * 1024, std::max<VkDeviceSize>(16u, gpu_props.limits.minUniformBufferOffsetAlignment), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ImplementationQuirks::get().staging_need_device_local);
		managers.ubo.SetSpillRegionSize(VULKAN_MAX_UBO_SIZE);
		managers.staging.Init(this, 64 * 1024, std::max<VkDeviceSize>(16u, gpu_props.limits.optimalBufferCopyOffsetAlignment),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			false);

		InitPipelineCache(initial_cache_data, initial_cache_size);
	}

	/*void Device::InitBindless()
	{
		if (!ext->supports_descriptor_indexing)
			return;

		DescriptorSetLayout layout;

		layout.array_size[0] = DescriptorSetLayout::UNSIZED_ARRAY;
		for (unsigned i = 1; i < VULKAN_NUM_BINDINGS; i++)
			layout.array_size[i] = 1;

		layout.separate_image_mask = 1;

		layout.binding_stages[0] = VK_SHADER_STAGE_ALL;
		bindless_sampled_image_allocator_integer = CreateSetAllocator(layout);
		layout.fp_mask = 1;
		bindless_sampled_image_allocator_fp = CreateSetAllocator(layout);
	}*/

	void Device::InitTimelineSemaphores()
	{
		if (!ext->timeline_semaphore_features.timelineSemaphore)
			return;

		VkSemaphoreTypeCreateInfoKHR type_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR };
		VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		info.pNext = &type_info;
		type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
		type_info.initialValue = 0;
		if (table->vkCreateSemaphore(device, &info, nullptr, &graphics.timeline_semaphore) != VK_SUCCESS)
			QM_LOG_ERROR("Failed to create timeline semaphore.\n");
		if (table->vkCreateSemaphore(device, &info, nullptr, &compute.timeline_semaphore) != VK_SUCCESS)
			QM_LOG_ERROR("Failed to create timeline semaphore.\n");
		if (table->vkCreateSemaphore(device, &info, nullptr, &transfer.timeline_semaphore) != VK_SUCCESS)
			QM_LOG_ERROR("Failed to create timeline sempahore.\n");
	}

	void Device::InitStockSamplers()
	{
		SamplerCreateInfo info = {};
		info.max_lod = VK_LOD_CLAMP_NONE;
		info.max_anisotropy = 1.0f;

		for (unsigned i = 0; i < static_cast<unsigned>(StockSampler::Count); i++)
		{
			auto mode = static_cast<StockSampler>(i);

			switch (mode)
			{
			case StockSampler::NearestShadow:
			case StockSampler::LinearShadow:
				info.compare_enable = true;
				info.compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
				break;

			default:
				info.compare_enable = false;
				break;
			}

			switch (mode)
			{
			case StockSampler::TrilinearClamp:
			case StockSampler::TrilinearWrap:
				info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				break;

			default:
				info.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
				break;
			}

			switch (mode)
			{
			case StockSampler::LinearClamp:
			case StockSampler::LinearWrap:
			case StockSampler::TrilinearClamp:
			case StockSampler::TrilinearWrap:
			case StockSampler::LinearShadow:
				info.mag_filter = VK_FILTER_LINEAR;
				info.min_filter = VK_FILTER_LINEAR;
				break;

			default:
				info.mag_filter = VK_FILTER_NEAREST;
				info.min_filter = VK_FILTER_NEAREST;
				break;
			}

			switch (mode)
			{
			default:
			case StockSampler::LinearWrap:
			case StockSampler::NearestWrap:
			case StockSampler::TrilinearWrap:
				info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;
				break;

			case StockSampler::LinearClamp:
			case StockSampler::NearestClamp:
			case StockSampler::TrilinearClamp:
			case StockSampler::NearestShadow:
			case StockSampler::LinearShadow:
				info.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				info.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				info.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				break;
			}

			samplers[i] = CreateSampler(info);
		}
	}

	static void RequestBlock(Device& device, BufferBlock& block, VkDeviceSize size, BufferPool& pool, std::vector<BufferBlock>* dma, std::vector<BufferBlock>& recycle)
	{
		if (block.mapped)
			device.UnmapHostBuffer(*block.cpu, MEMORY_ACCESS_WRITE_BIT);

		if (block.offset == 0)
		{
			if (block.size == pool.GetBlockSize())
				pool.RecycleBlock(move(block));
		}
		else
		{
			if (block.cpu != block.gpu)
			{
				VK_ASSERT(dma);
				dma->push_back(block);
			}

			if (block.size == pool.GetBlockSize())
				recycle.push_back(block);
		}

		if (size)
			block = pool.RequestBlock(size);
		else
			block = {};
	}

	void Device::RequestVertexBlock(BufferBlock& block, VkDeviceSize size)
	{
		LOCK();
		RequestVertexBlockNolock(block, size);
	}

	void Device::RequestVertexBlockNolock(BufferBlock& block, VkDeviceSize size)
	{
		RequestBlock(*this, block, size, managers.vbo, &dma.vbo, Frame().vbo_blocks);
	}

	void Device::RequestIndexBlock(BufferBlock& block, VkDeviceSize size)
	{
		LOCK();
		RequestIndexBlockNolock(block, size);
	}

	void Device::RequestIndexBlockNolock(BufferBlock& block, VkDeviceSize size)
	{
		RequestBlock(*this, block, size, managers.ibo, &dma.ibo, Frame().ibo_blocks);
	}

	void Device::RequestUniformBlock(BufferBlock& block, VkDeviceSize size)
	{
		LOCK();
		RequestUniformBlockNolock(block, size);
	}

	void Device::RequestUniformBlockNolock(BufferBlock& block, VkDeviceSize size)
	{
		RequestBlock(*this, block, size, managers.ubo, &dma.ubo, Frame().ubo_blocks);
	}

	void Device::RequestStagingBlock(BufferBlock& block, VkDeviceSize size)
	{
		LOCK();
		RequestStagingBlockNolock(block, size);
	}

	void Device::RequestStagingBlockNolock(BufferBlock& block, VkDeviceSize size)
	{
		RequestBlock(*this, block, size, managers.staging, nullptr, Frame().staging_blocks);
	}

	Fence Device::RequestLegacyFence()
	{
		VkFence fence = managers.fence.RequestClearedFence();
		return Fence(handle_pool.fences.allocate(this, fence));
	}

	void Device::FlushFrame(CommandBuffer::Type type)
	{
		if (type == CommandBuffer::Type::AsyncTransfer)
			SyncBufferBlocks();
		SubmitQueue(type, nullptr, 0, nullptr);
	}

	void Device::SyncBufferBlocks()
	{
		if (dma.vbo.empty() && dma.ibo.empty() && dma.ubo.empty())
			return;

		VkBufferUsageFlags usage = 0;

		auto cmd = RequestCommandBufferNolock(get_thread_index(), CommandBuffer::Type::AsyncTransfer);

		for (auto& block : dma.vbo)
		{
			VK_ASSERT(block.offset != 0);
			cmd->CopyBuffer(*block.gpu, 0, *block.cpu, 0, block.offset);
			usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		}

		for (auto& block : dma.ibo)
		{
			VK_ASSERT(block.offset != 0);
			cmd->CopyBuffer(*block.gpu, 0, *block.cpu, 0, block.offset);
			usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		}

		for (auto& block : dma.ubo)
		{
			VK_ASSERT(block.offset != 0);
			cmd->CopyBuffer(*block.gpu, 0, *block.cpu, 0, block.offset);
			usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		}

		dma.vbo.clear();
		dma.ibo.clear();
		dma.ubo.clear();

		// Do not flush graphics or compute in this context.
		// We must be able to inject semaphores into all currently enqueued graphics / compute.
		SubmitStaging(cmd, usage, false);
	}

	void Device::EndFrameContext()
	{
		DRAIN_FRAME_LOCK();
		EndFrameNolock();
	}

	void Device::EndFrameNolock()
	{
		// Kept handles alive until end-of-frame, free now if appropriate.
		for (auto& image : Frame().keep_alive_images)
		{
			image->SetInternalSyncObject();
			image->GetView().SetInternalSyncObject();
		}
		Frame().keep_alive_images.clear();

		// Make sure we have a fence which covers all submissions in the frame.
		InternalFence fence;

		if (transfer.need_fence || !Frame().transfer_submissions.empty())
		{
			SubmitQueue(CommandBuffer::Type::AsyncTransfer, &fence, 0, nullptr);
			if (fence.fence != VK_NULL_HANDLE)
			{
				Frame().wait_fences.push_back(fence.fence);
				Frame().recycle_fences.push_back(fence.fence);
			}
			transfer.need_fence = false;
		}

		if (graphics.need_fence || !Frame().graphics_submissions.empty())
		{
			SubmitQueue(CommandBuffer::Type::Generic, &fence, 0, nullptr);
			if (fence.fence != VK_NULL_HANDLE)
			{
				Frame().wait_fences.push_back(fence.fence);
				Frame().recycle_fences.push_back(fence.fence);
			}
			graphics.need_fence = false;
		}

		if (compute.need_fence || !Frame().compute_submissions.empty())
		{
			SubmitQueue(CommandBuffer::Type::AsyncCompute, &fence, 0, nullptr);
			if (fence.fence != VK_NULL_HANDLE)
			{
				Frame().wait_fences.push_back(fence.fence);
				Frame().recycle_fences.push_back(fence.fence);
			}
			compute.need_fence = false;
		}
	}

	void Device::FlushFrame()
	{
		LOCK();
		FlushFrameNolock();
	}

	void Device::FlushFrameNolock()
	{
		FlushFrame(CommandBuffer::Type::AsyncTransfer);
		FlushFrame(CommandBuffer::Type::Generic);
		FlushFrame(CommandBuffer::Type::AsyncCompute);
	}

	void Device::SetAcquireSemaphore(unsigned index, Semaphore acquire)
	{
		wsi.acquire = move(acquire);
		wsi.index = index;
		wsi.touched = false;
		wsi.consumed = false;

		if (wsi.acquire)
		{
			wsi.acquire->SetInternalSyncObject();
			VK_ASSERT(wsi.acquire->IsSignalled());
		}
	}

	Semaphore Device::ConsumeReleaseSemaphore()
	{
		auto ret = move(wsi.release);
		wsi.release.Reset();
		return ret;
	}

	const Sampler& Device::GetStockSampler(StockSampler sampler) const
	{
		return *samplers[static_cast<unsigned>(sampler)];
	}

	bool Device::SwapchainTouched() const
	{
		return wsi.touched;
	}

	uint32_t Device::GetQueueFamilyIndex(CommandBuffer::Type type) const
	{
		CommandBuffer::Type physical_type = GetPhysicalQueueType(type);
		switch (physical_type)
		{
		case Vulkan::CommandBuffer::Type::Generic:       return graphics_queue_family_index;
		case Vulkan::CommandBuffer::Type::AsyncCompute:  return compute_queue_family_index;
		case Vulkan::CommandBuffer::Type::AsyncTransfer: return transfer_queue_family_index;
		}

		QM_LOG_ERROR("Unrecognized command buffer type");
		return graphics_queue_family_index;
	}

	VkQueue Device::GetQueue(CommandBuffer::Type type) const
	{
		CommandBuffer::Type physical_type = GetPhysicalQueueType(type);
		switch (physical_type)
		{
		case Vulkan::CommandBuffer::Type::Generic:       return graphics_queue;
		case Vulkan::CommandBuffer::Type::AsyncCompute:  return compute_queue;
		case Vulkan::CommandBuffer::Type::AsyncTransfer: return transfer_queue;
		}

		QM_LOG_ERROR("Unrecognized command buffer type");
		return graphics_queue;
	}

	Device::~Device()
	{
		WaitIdle();

		wsi.acquire.Reset();
		wsi.release.Reset();
		wsi.swapchain.clear();

		if (pipeline_cache != VK_NULL_HANDLE)
		{
			table->vkDestroyPipelineCache(device, pipeline_cache, nullptr);
		}

		framebuffer_allocator.Clear();
		transient_allocator.Clear();
		physical_allocator.Clear();
		for (auto& sampler : samplers)
			sampler.Reset();

		//DeinitBindless();
		DeinitTimelineSemaphores();

	}

	void Device::DeinitTimelineSemaphores()
	{
		if (graphics.timeline_semaphore != VK_NULL_HANDLE)
			table->vkDestroySemaphore(device, graphics.timeline_semaphore, nullptr);
		if (compute.timeline_semaphore != VK_NULL_HANDLE)
			table->vkDestroySemaphore(device, compute.timeline_semaphore, nullptr);
		if (transfer.timeline_semaphore != VK_NULL_HANDLE)
			table->vkDestroySemaphore(device, transfer.timeline_semaphore, nullptr);

		graphics.timeline_semaphore = VK_NULL_HANDLE;
		compute.timeline_semaphore = VK_NULL_HANDLE;
		transfer.timeline_semaphore = VK_NULL_HANDLE;

		// Make sure we don't accidentally try to wait for these after we destroy the semaphores.
		for (auto& frame : per_frame)
		{
			frame->timeline_fence_graphics = 0;
			frame->timeline_fence_compute = 0;
			frame->timeline_fence_transfer = 0;
			frame->graphics_timeline_semaphore = VK_NULL_HANDLE;
			frame->compute_timeline_semaphore = VK_NULL_HANDLE;
			frame->transfer_timeline_semaphore = VK_NULL_HANDLE;
		}
	}


	/*void Device::DeinitBindless()
	{
		FreeSetAllocator(bindless_sampled_image_allocator_fp);
		FreeSetAllocator(bindless_sampled_image_allocator_integer);
	}*/

	void Device::InitFrameContexts(unsigned count)
	{
		DRAIN_FRAME_LOCK();
		WaitIdleNolock();

		// Clear out caches which might contain stale data from now on.
		framebuffer_allocator.Clear();
		transient_allocator.Clear();
		physical_allocator.Clear();
		per_frame.clear();

		for (unsigned i = 0; i < count; i++)
		{
			auto frame = unique_ptr<PerFrame>(new PerFrame(this, i));
			per_frame.emplace_back(move(frame));
		}
	}

	void Device::InitExternalSwapchain(const vector<ImageHandle>& swapchain_images)
	{
		DRAIN_FRAME_LOCK();
		wsi.swapchain.clear();
		WaitIdleNolock();

		wsi.index = 0;
		wsi.touched = false;
		wsi.consumed = false;
		for (auto& image : swapchain_images)
		{
			wsi.swapchain.push_back(image);
			if (image)
			{
				wsi.swapchain.back()->SetInternalSyncObject();
				wsi.swapchain.back()->GetView().SetInternalSyncObject();
			}
		}
	}

	void Device::InitSwapchain(const vector<VkImage>& swapchain_images, unsigned width, unsigned height, VkFormat format)
	{
		DRAIN_FRAME_LOCK();
		wsi.swapchain.clear();
		WaitIdleNolock();

		const auto info = ImageCreateInfo::RenderTarget(width, height, format);

		wsi.index = 0;
		wsi.touched = false;
		wsi.consumed = false;
		for (auto& image : swapchain_images)
		{
			VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			view_info.image = image;
			view_info.format = format;
			view_info.components.r = VK_COMPONENT_SWIZZLE_R;
			view_info.components.g = VK_COMPONENT_SWIZZLE_G;
			view_info.components.b = VK_COMPONENT_SWIZZLE_B;
			view_info.components.a = VK_COMPONENT_SWIZZLE_A;
			view_info.subresourceRange.aspectMask = FormatToAspectMask(format);
			view_info.subresourceRange.baseMipLevel = 0;
			view_info.subresourceRange.baseArrayLayer = 0;
			view_info.subresourceRange.levelCount = 1;
			view_info.subresourceRange.layerCount = 1;
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;

			VkImageView image_view;
			if (table->vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
				QM_LOG_ERROR("Failed to create view for backbuffer.");

			auto backbuffer = ImageHandle(handle_pool.images.allocate(this, image, image_view, DeviceAllocation{}, info, VK_IMAGE_VIEW_TYPE_2D));
			backbuffer->SetInternalSyncObject();
			backbuffer->DisownImage();
			backbuffer->GetView().SetInternalSyncObject();
			wsi.swapchain.push_back(backbuffer);
			backbuffer->SetSwapchainLayout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		}
	}

	////////////////////////////////
	//PerFrame Stuff////////////////
	////////////////////////////////

	PerFrame::PerFrame(Device* device_, unsigned frame_index_)
		: device(*device_)
		, frame_index(frame_index_)
		, table(device_->GetDeviceTable())
		, managers(device_->managers)
	{
		graphics_timeline_semaphore = device.graphics.timeline_semaphore;
		compute_timeline_semaphore = device.compute.timeline_semaphore;
		transfer_timeline_semaphore = device.transfer.timeline_semaphore;

		unsigned count = device_->num_thread_indices;
		graphics_cmd_pool.reserve(count);
		compute_cmd_pool.reserve(count);
		transfer_cmd_pool.reserve(count);
		for (unsigned i = 0; i < count; i++)
		{
			graphics_cmd_pool.emplace_back(device_, device_->graphics_queue_family_index);
			compute_cmd_pool.emplace_back(device_, device_->compute_queue_family_index);
			transfer_cmd_pool.emplace_back(device_, device_->transfer_queue_family_index);
		}
	}

	void Device::KeepHandleAlive(ImageHandle handle)
	{
		LOCK();
		Frame().keep_alive_images.push_back(move(handle));
	}

#ifdef VULKAN_DEBUG

	template <typename T, typename U>
	static inline bool exists(const T& container, const U& value)
	{
		return std::find(std::begin(container), std::end(container), value) != std::end(container);
	}

#endif

	void Device::ResetFence(VkFence fence, bool observed_wait)
	{
		LOCK();
		ResetFenceNolock(fence, observed_wait);
	}

	void Device::DestroyBuffer(VkBuffer buffer, const DeviceAllocation& allocation)
	{
		LOCK();
		DestroyBufferNolock(buffer, allocation);
	}

	void Device::DestroyProgramNoLock(Program* program)
	{
		Frame().destroyed_programs.push_back(program);
	}

	void Device::DestroyShader(Shader* shader)
	{
#ifdef QM_VULKAN_MT
		std::lock_guard holder_{ lock.shader_lock };
#endif
		Frame().destroyed_shaders.push_back(shader);
	}

	void Device::DestroyBufferView(VkBufferView view)
	{
		LOCK();
		DestroyBufferViewNolock(view);
	}

	void Device::DestroyEvent(VkEvent event)
	{
		LOCK();
		DestroyEventNolock(event);
	}

	void Device::DestroyFramebuffer(VkFramebuffer framebuffer)
	{
		LOCK();
		DestroyFramebufferNolock(framebuffer);
	}

	void Device::DestroyImage(VkImage image, const DeviceAllocation& allocation)
	{
		LOCK();
		DestroyImageNolock(image, allocation);
	}

	void Device::DestroySemaphore(VkSemaphore semaphore)
	{
		LOCK();
		DestroySemaphoreNolock(semaphore);
	}

	void Device::RecycleSemaphore(VkSemaphore semaphore)
	{
		LOCK();
		RecycleSemaphoreNolock(semaphore);
	}

	void Device::DestroySampler(VkSampler sampler)
	{
		LOCK();
		DestroySamplerNolock(sampler);
	}

	void Device::DestroyImageView(VkImageView view)
	{
		LOCK();
		DestroyImageViewNolock(view);
	}

	void Device::DestroyImageViewNolock(VkImageView view)
	{
		VK_ASSERT(!exists(Frame().destroyed_image_views, view));
		Frame().destroyed_image_views.push_back(view);
	}

	void Device::DestroyBufferViewNolock(VkBufferView view)
	{
		VK_ASSERT(!exists(Frame().destroyed_buffer_views, view));
		Frame().destroyed_buffer_views.push_back(view);
	}

	void Device::DestroySemaphoreNolock(VkSemaphore semaphore)
	{
		VK_ASSERT(!exists(Frame().destroyed_semaphores, semaphore));
		Frame().destroyed_semaphores.push_back(semaphore);
	}

	void Device::RecycleSemaphoreNolock(VkSemaphore semaphore)
	{
		VK_ASSERT(!exists(Frame().recycled_semaphores, semaphore));
		Frame().recycled_semaphores.push_back(semaphore);
	}

	void Device::DestroyEventNolock(VkEvent event)
	{
		VK_ASSERT(!exists(Frame().recycled_events, event));
		Frame().recycled_events.push_back(event);
	}

	void Device::ResetFenceNolock(VkFence fence, bool observed_wait)
	{
		if (observed_wait)
		{
			table->vkResetFences(device, 1, &fence);
			managers.fence.RecycleFence(fence);
		}
		else
			Frame().recycle_fences.push_back(fence);
	}

	void Device::DestroyImageNolock(VkImage image, const DeviceAllocation& allocation)
	{
		//VK_ASSERT(!exists(Frame().destroyed_images, std::make_pair(image, allocation)));
		Frame().destroyed_images.push_back(std::make_pair(image, allocation));
	}

	void Device::DestroyBufferNolock(VkBuffer buffer, const DeviceAllocation& allocation)
	{
		//VK_ASSERT(!exists(Frame().destroyed_buffers, std::make_pair(buffer, allocation)));
		Frame().destroyed_buffers.push_back(std::make_pair(buffer, allocation));
	}

	void Device::DestroySamplerNolock(VkSampler sampler)
	{
		VK_ASSERT(!exists(Frame().destroyed_samplers, sampler));
		Frame().destroyed_samplers.push_back(sampler);
	}

	void Device::DestroyFramebufferNolock(VkFramebuffer framebuffer)
	{
		VK_ASSERT(!exists(Frame().destroyed_framebuffers, framebuffer));
		Frame().destroyed_framebuffers.push_back(framebuffer);
	}

	void PerFrame::Begin()
	{
		VkDevice vkdevice = device.GetDevice();

		if (device.GetDeviceExtensions().timeline_semaphore_features.timelineSemaphore && graphics_timeline_semaphore && compute_timeline_semaphore && transfer_timeline_semaphore)
		{
			VkSemaphoreWaitInfoKHR info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR };
			const VkSemaphore semaphores[3] = { graphics_timeline_semaphore, compute_timeline_semaphore, transfer_timeline_semaphore };
			const uint64_t values[3] = { timeline_fence_graphics, timeline_fence_compute, timeline_fence_transfer };

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			if (device.GetDeviceExtensions().timeline_semaphore_features.timelineSemaphore)
			{
				QM_LOG_INFO("Waiting for graphics (%p) %u\n",
					reinterpret_cast<void*>(graphics_timeline_semaphore),
					unsigned(timeline_fence_graphics));
				QM_LOG_INFO("Waiting for compute (%p) %u\n",
					reinterpret_cast<void*>(compute_timeline_semaphore),
					unsigned(timeline_fence_compute));
				QM_LOG_INFO("Waiting for transfer (%p) %u\n",
					reinterpret_cast<void*>(transfer_timeline_semaphore),
					unsigned(timeline_fence_transfer));
			}
#endif

			info.pSemaphores = semaphores;
			info.pValues = values;
			info.semaphoreCount = 3;
			table.vkWaitSemaphoresKHR(vkdevice, &info, UINT64_MAX);
		}

		// If we're using timeline semaphores, these paths should never be hit.
		if (!wait_fences.empty())
		{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			for (auto& fence : wait_fences)
				QM_LOG_INFO("Waiting for Fence: %llx\n", reinterpret_cast<unsigned long long>(fence));
#endif
			table.vkWaitForFences(vkdevice, wait_fences.size(), wait_fences.data(), VK_TRUE, UINT64_MAX);
			wait_fences.clear();
		}

		// If we're using timeline semaphores, these paths should never be hit.
		if (!recycle_fences.empty())
		{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			for (auto& fence : recycle_fences)
				QM_LOG_INFO("Recycling Fence: %llx\n", reinterpret_cast<unsigned long long>(fence));
#endif
			table.vkResetFences(vkdevice, recycle_fences.size(), recycle_fences.data());
			for (auto& fence : recycle_fences)
				managers.fence.RecycleFence(fence);
			recycle_fences.clear();
		}

		for (auto& pool : graphics_cmd_pool)
			pool.Begin();
		for (auto& pool : compute_cmd_pool)
			pool.Begin();
		for (auto& pool : transfer_cmd_pool)
			pool.Begin();

		for (auto& framebuffer : destroyed_framebuffers)
			table.vkDestroyFramebuffer(vkdevice, framebuffer, nullptr);
		for (auto& sampler : destroyed_samplers)
			table.vkDestroySampler(vkdevice, sampler, nullptr);
		for (auto& view : destroyed_image_views)
			table.vkDestroyImageView(vkdevice, view, nullptr);
		for (auto& view : destroyed_buffer_views)
			table.vkDestroyBufferView(vkdevice, view, nullptr);
		for (auto& image : destroyed_images)
			device.managers.memory.FreeImage(image.first, image.second);
		for (auto& buffer : destroyed_buffers)
			device.managers.memory.FreeBuffer(buffer.first, buffer.second);
		for (auto& semaphore : destroyed_semaphores)
			table.vkDestroySemaphore(vkdevice, semaphore, nullptr);
		for (auto& semaphore : recycled_semaphores)
		{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Recycling semaphore: %llx\n", reinterpret_cast<unsigned long long>(semaphore));
#endif
			managers.semaphore.RecycleSemaphore(semaphore);
		}
		for (auto& event : recycled_events)
			managers.event.RecycleEvent(event);

		{
#ifdef QM_VULKAN_MT
			std::lock_guard program_holder_{ device.lock.program_lock };
#endif
			for (auto& program : destroyed_programs)
				device.handle_pool.programs.free(program);
		}

		{
#ifdef QM_VULKAN_MT
			std::lock_guard shader_holder_{ device.lock.shader_lock };
#endif
			for (auto& shader : destroyed_shaders)
				device.handle_pool.shaders.free(shader);
		}

		for (auto& block : vbo_blocks)
			managers.vbo.RecycleBlock(move(block));
		for (auto& block : ibo_blocks)
			managers.ibo.RecycleBlock(move(block));
		for (auto& block : ubo_blocks)
			managers.ubo.RecycleBlock(move(block));
		for (auto& block : staging_blocks)
			managers.staging.RecycleBlock(move(block));

		vbo_blocks.clear();
		ibo_blocks.clear();
		ubo_blocks.clear();
		staging_blocks.clear();

		destroyed_framebuffers.clear();
		destroyed_samplers.clear();
		destroyed_image_views.clear();
		destroyed_buffer_views.clear();
		destroyed_images.clear();
		destroyed_buffers.clear();
		destroyed_semaphores.clear();
		recycled_semaphores.clear();
		recycled_events.clear();

		destroyed_shaders.clear();
		destroyed_programs.clear();

		//int64_t min_timestamp_us = std::numeric_limits<int64_t>::max();
		//int64_t max_timestamp_us = 0;
	}

	PerFrame::~PerFrame()
	{
		Begin();
	}

	/////////////////////////////
	//End of Per frame stuff/////
	/////////////////////////////

	PipelineEvent Device::RequestPipelineEvent()
	{
		return PipelineEvent(handle_pool.events.allocate(this, managers.event.RequestClearedEvent()));
	}

	void Device::ClearWaitSemaphores()
	{
		for (auto& sem : graphics.wait_semaphores)
			table->vkDestroySemaphore(device, sem->Consume(), nullptr);
		for (auto& sem : compute.wait_semaphores)
			table->vkDestroySemaphore(device, sem->Consume(), nullptr);
		for (auto& sem : transfer.wait_semaphores)
			table->vkDestroySemaphore(device, sem->Consume(), nullptr);

		graphics.wait_semaphores.clear();
		graphics.wait_stages.clear();
		compute.wait_semaphores.clear();
		compute.wait_stages.clear();
		transfer.wait_semaphores.clear();
		transfer.wait_stages.clear();
	}

	void Device::WaitIdle()
	{
		DRAIN_FRAME_LOCK();
		WaitIdleNolock();
	}

	void Device::WaitIdleNolock()
	{
		if (!per_frame.empty())
			EndFrameNolock();

		if (device != VK_NULL_HANDLE)
		{
			if (queue_lock_callback)
				queue_lock_callback();
			auto result = table->vkDeviceWaitIdle(device);
			if (result != VK_SUCCESS)
				QM_LOG_ERROR("vkDeviceWaitIdle failed with code: %d\n", result);
			if (queue_unlock_callback)
				queue_unlock_callback();
		}

		ClearWaitSemaphores();

		// Free memory for buffer pools.
		managers.vbo.Reset();
		managers.ubo.Reset();
		managers.ibo.Reset();
		managers.staging.Reset();
		for (auto& frame : per_frame)
		{
			frame->vbo_blocks.clear();
			frame->ibo_blocks.clear();
			frame->ubo_blocks.clear();
			frame->staging_blocks.clear();
		}

		framebuffer_allocator.Clear();
		transient_allocator.Clear();
		physical_allocator.Clear();

		descriptor_set_allocators.for_each([](DescriptorSetAllocator* allocator) {
			allocator->Clear();
			});

		for (auto& frame : per_frame)
		{
			// We have done WaitIdle, no need to wait for extra fences, it's also not safe.
			frame->wait_fences.clear();
			frame->Begin();
		}
	}

	void Device::NextFrameContext()
	{
		DRAIN_FRAME_LOCK();

		UpdateInvalidProgramsNoLock();

		// Flush the frame here as we might have pending staging command buffers from init stage.
		EndFrameNolock();

		framebuffer_allocator.BeginFrame();
		transient_allocator.BeginFrame();
		physical_allocator.BeginFrame();

		descriptor_set_allocators.for_each([](DescriptorSetAllocator* allocator) {
			allocator->BeginFrame();
			});

		VK_ASSERT(!per_frame.empty());

		frame_context_index++;
		if (frame_context_index >= per_frame.size())
			frame_context_index = 0;

		Frame().Begin();
	}

	void Device::AddFrameCounterNolock()
	{
		lock.counter++;
	}

	void Device::DecrementFrameCounterNolock()
	{
		VK_ASSERT(lock.counter > 0);
		lock.counter--;
#ifdef QM_VULKAN_MT
		lock.cond.notify_one();
#endif
	}

	////////////////////////////
	//Buffer Creation///////////
	////////////////////////////

	void Device::FillBufferSharingIndices(VkBufferCreateInfo& info, uint32_t* sharing_indices)
	{
		//If different queues have different queue families
		if (graphics_queue_family_index != compute_queue_family_index || graphics_queue_family_index != transfer_queue_family_index)
		{
			// For buffers, always just use CONCURRENT access modes,
			// so we don't have to deal with acquire/release barriers in async compute.
			info.sharingMode = VK_SHARING_MODE_CONCURRENT;

			sharing_indices[info.queueFamilyIndexCount++] = graphics_queue_family_index;

			//If the graphics queue and compute queues aren't from the same family
			if (graphics_queue_family_index != compute_queue_family_index)
				sharing_indices[info.queueFamilyIndexCount++] = compute_queue_family_index;

			if (graphics_queue_family_index != transfer_queue_family_index && compute_queue_family_index != transfer_queue_family_index)
			{
				sharing_indices[info.queueFamilyIndexCount++] = transfer_queue_family_index;
			}

			info.pQueueFamilyIndices = sharing_indices;
		}
	}

	BufferViewHandle Device::CreateBufferView(const BufferViewCreateInfo& view_info)
	{
		VkBufferViewCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
		info.buffer = view_info.buffer->GetBuffer();
		info.format = view_info.format;
		info.offset = view_info.offset;
		info.range = view_info.range;

		VkBufferView view;
		auto res = table->vkCreateBufferView(device, &info, nullptr, &view);
		if (res != VK_SUCCESS)
			return BufferViewHandle(nullptr);

		return BufferViewHandle(handle_pool.buffer_views.allocate(this, view, view_info));
	}

	BufferHandle Device::CreateBuffer(const BufferCreateInfo& create_info, ResourceQueueOwnershipFlags ownership, const void* initial)
	{

		bool is_async_graphics_on_compute_queue = GetPhysicalQueueType(CommandBuffer::Type::AsyncGraphics) == CommandBuffer::Type::AsyncCompute;
		bool is_concurrent_graphics = (ownership & RESOURCE_CONCURRENT_GENERIC) || (!is_async_graphics_on_compute_queue && (ownership & RESOURCE_CONCURRENT_ASYNC_GRAPHICS));
		bool is_concurrent_compute = (ownership & RESOURCE_CONCURRENT_ASYNC_COMPUTE) || (is_async_graphics_on_compute_queue && (ownership & RESOURCE_CONCURRENT_ASYNC_GRAPHICS));
		bool is_concurrent_transfer = ownership & RESOURCE_CONCURRENT_ASYNC_TRANSFER;

		bool is_exclusive = false;
		uint32_t exclusive_queue_family_index;
		CommandBuffer::Type exclusive_owner;

		if (ownership & RESOURCE_EXCLUSIVE_GENERIC)
		{
			VK_ASSERT(!is_exclusive);
			is_exclusive = true;
			exclusive_queue_family_index = graphics_queue_family_index;
			exclusive_owner = CommandBuffer::Type::Generic;
		}
		else if (ownership & RESOURCE_EXCLUSIVE_ASYNC_GRAPHICS)
		{
			VK_ASSERT(!is_exclusive);
			is_exclusive = true;
			exclusive_queue_family_index = is_async_graphics_on_compute_queue ? compute_queue_family_index : graphics_queue_family_index;
			exclusive_owner = CommandBuffer::Type::AsyncGraphics;
		}
		else if (ownership & RESOURCE_EXCLUSIVE_ASYNC_TRANSFER)
		{
			VK_ASSERT(!is_exclusive);
			is_exclusive = true;
			exclusive_queue_family_index = transfer_queue_family_index;
			exclusive_owner = CommandBuffer::Type::AsyncTransfer;
		}
		else if (ownership & RESOURCE_EXCLUSIVE_ASYNC_COMPUTE)
		{
			VK_ASSERT(!is_exclusive);
			is_exclusive = true;
			exclusive_queue_family_index = compute_queue_family_index;
			exclusive_owner = CommandBuffer::Type::AsyncCompute;
		}

		VkBuffer buffer;
		DeviceAllocation allocation;

		bool zero_initialize = (create_info.misc & BUFFER_MISC_ZERO_INITIALIZE_BIT) != 0;
		if (initial && zero_initialize)
		{
			QM_LOG_ERROR("Cannot initialize buffer with data and Clear.\n");
			return BufferHandle{};
		}

		VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		info.size = create_info.size;
		info.usage = create_info.usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkPipelineStageFlags possible_buffer_stages = BufferUsageToPossibleStages(info.usage);
		VkAccessFlags possible_buffer_access = BufferUsageToPossibleAccess(info.usage);

		uint32_t sharing_indices[3];

		// Deduce sharing mode

		if (is_exclusive)
		{
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			info.pQueueFamilyIndices = nullptr;
			info.queueFamilyIndexCount = 0;
		}
		else
		{
			uint32_t queueFamilyCount = 0;
			const auto add_unique_family = [&](uint32_t family) {
				for (uint32_t i = 0; i < queueFamilyCount; i++)
				{
					if (sharing_indices[i] == family)
						return;
				}
				sharing_indices[queueFamilyCount++] = family;
			};

			if (ownership & RESOURCE_CONCURRENT_GENERIC)
				add_unique_family(graphics_queue_family_index);
			if (ownership & RESOURCE_CONCURRENT_ASYNC_GRAPHICS)
				add_unique_family(is_async_graphics_on_compute_queue ? compute_queue_family_index : graphics_queue_family_index);
			if (ownership & RESOURCE_CONCURRENT_ASYNC_COMPUTE)
				add_unique_family(compute_queue_family_index);
			if (((initial || zero_initialize) && create_info.domain == BufferDomain::Device) || (ownership & RESOURCE_CONCURRENT_ASYNC_TRANSFER) != 0)
				add_unique_family(transfer_queue_family_index);

			if (queueFamilyCount > 1)
			{
				info.sharingMode = VK_SHARING_MODE_CONCURRENT;
				info.pQueueFamilyIndices = sharing_indices;
				info.queueFamilyIndexCount = queueFamilyCount;
			}
			else
			{
				info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				info.pQueueFamilyIndices = nullptr;
				info.queueFamilyIndexCount = 0;
			}
		}

		VmaAllocationCreateInfo alloc_info{};
		if (create_info.domain == BufferDomain::Host) 
		{
			alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			alloc_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		}
		else if (create_info.domain == BufferDomain::Device)
		{
			alloc_info.flags = 0;
			alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		}
		else if (create_info.domain == BufferDomain::CachedHost)
		{
			alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			alloc_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		}
		else if (create_info.domain == BufferDomain::LinkedDeviceHost)
		{
			alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			alloc_info.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		}

		if(!managers.memory.AllocateBuffer(info, alloc_info, &buffer, &allocation))
			return BufferHandle(nullptr);

		
		auto tmpinfo = create_info;
		tmpinfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		BufferHandle handle(handle_pool.buffers.allocate(this, buffer, allocation, tmpinfo));

		if (create_info.domain == BufferDomain::Device && (initial || zero_initialize) && !AllocationHasMemoryPropertyFlags(allocation, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
		{
			//CommandBufferHandle cmd;
			if (initial)
			{
				auto staging_info = create_info;
				staging_info.domain = BufferDomain::Host;
				auto staging_buffer = CreateBuffer(staging_info, RESOURCE_EXCLUSIVE_ASYNC_TRANSFER, initial);

				CommandBufferHandle cmd = RequestCommandBuffer(CommandBuffer::Type::AsyncTransfer);
				cmd->CopyBuffer(*handle, *staging_buffer);

				if (is_exclusive)
				{
					VkBufferMemoryBarrier release{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
					release.buffer = handle->GetBuffer();
					release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					release.dstAccessMask = 0;
					release.srcQueueFamilyIndex = transfer_queue_family_index;
					release.dstQueueFamilyIndex = exclusive_queue_family_index;
					release.offset = 0;
					release.size = VK_WHOLE_SIZE;

					cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, nullptr, 1, &release, 0, nullptr);

					Semaphore sem;
					Submit(cmd, nullptr, 1, &sem);
					AddWaitSemaphore(exclusive_owner, sem, possible_buffer_stages, true);

					CommandBufferHandle target_cmd = RequestCommandBuffer(exclusive_owner);

					VkBufferMemoryBarrier acquire = release;
					acquire.srcAccessMask = 0;
					acquire.dstAccessMask = possible_buffer_access;

					target_cmd->Barrier(possible_buffer_stages, possible_buffer_stages, 0, nullptr, 1, &acquire, 0, nullptr);

					Submit(target_cmd);
				}
				else
				{
					cmd->BufferBarrier(*handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);

					bool compute_sem_needed = compute_queue != transfer_queue && is_concurrent_compute;
					bool graphics_sem_needed = graphics_queue != transfer_queue && is_concurrent_graphics;

					if (compute_sem_needed && !graphics_sem_needed)
					{

						Semaphore sem[1];
						Submit(cmd, nullptr, 1, sem);
						AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
					}
					else if (!compute_sem_needed && graphics_sem_needed)
					{
						Semaphore sem[1];
						Submit(cmd, nullptr, 1, sem);
						AddWaitSemaphore(CommandBuffer::Type::Generic, sem[0], possible_buffer_stages, true);
					}
					else if (compute_sem_needed && graphics_sem_needed)
					{
						Semaphore sem[2];
						Submit(cmd, nullptr, 2, sem);
						AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, false);
						AddWaitSemaphore(CommandBuffer::Type::AsyncTransfer, sem[1], possible_buffer_stages, true);
					}
					else
					{
						Submit(cmd);
					}
				}
			}
			else
			{
				CommandBufferHandle cmd = RequestCommandBuffer(CommandBuffer::Type::AsyncTransfer);
				cmd->FillBuffer(*handle, 0);

				if (is_exclusive)
				{
					VkBufferMemoryBarrier release{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
					release.buffer = handle->GetBuffer();
					release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					release.dstAccessMask = 0;
					release.srcQueueFamilyIndex = transfer_queue_family_index;
					release.dstQueueFamilyIndex = exclusive_queue_family_index;
					release.offset = 0;
					release.size = VK_WHOLE_SIZE;

					cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, nullptr, 1, &release, 0, nullptr);

					Semaphore sem;
					Submit(cmd, nullptr, 1, &sem);
					AddWaitSemaphore(exclusive_owner, sem, possible_buffer_stages, true);

					CommandBufferHandle target_cmd = RequestCommandBuffer(exclusive_owner);

					VkBufferMemoryBarrier acquire = release;
					acquire.srcAccessMask = 0;
					acquire.dstAccessMask = possible_buffer_access;

					target_cmd->Barrier(possible_buffer_stages, possible_buffer_stages, 0, nullptr, 1, &acquire, 0, nullptr);

					Submit(target_cmd);
				}
				else
				{
					cmd->BufferBarrier(*handle, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);

					bool compute_sem_needed = compute_queue != transfer_queue && is_concurrent_compute;
					bool graphics_sem_needed = graphics_queue != transfer_queue && is_concurrent_graphics;

					if (compute_sem_needed && !graphics_sem_needed)
					{

						Semaphore sem[1];
						Submit(cmd, nullptr, 1, sem);
						AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
					}
					else if (!compute_sem_needed && graphics_sem_needed)
					{
						Semaphore sem[1];
						Submit(cmd, nullptr, 1, sem);
						AddWaitSemaphore(CommandBuffer::Type::Generic, sem[0], possible_buffer_stages, true);
					}
					else if (compute_sem_needed && graphics_sem_needed)
					{
						Semaphore sem[2];
						Submit(cmd, nullptr, 2, sem);
						AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, false);
						AddWaitSemaphore(CommandBuffer::Type::AsyncTransfer, sem[1], possible_buffer_stages, true);
					}
					else
					{
						Submit(cmd);
					}
				}
			}
		}
		else if (initial || zero_initialize)
		{
			void* ptr = managers.memory.MapMemory(allocation, MEMORY_ACCESS_WRITE_BIT);
			if (!ptr)
				return BufferHandle(nullptr);

			if (initial)
				memcpy(ptr, initial, create_info.size);
			else
				memset(ptr, 0, create_info.size);
			managers.memory.UnmapMemory(allocation, MEMORY_ACCESS_WRITE_BIT);
		}
		return handle;
	}

	////////////////////////////
	//Sampler Creation//////////
	////////////////////////////

	static VkSamplerCreateInfo FillVkSamplerInfo(const SamplerCreateInfo& sampler_info)
	{
		VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

		info.magFilter = sampler_info.mag_filter;
		info.minFilter = sampler_info.min_filter;
		info.mipmapMode = sampler_info.mipmap_mode;
		info.addressModeU = sampler_info.address_mode_u;
		info.addressModeV = sampler_info.address_mode_v;
		info.addressModeW = sampler_info.address_mode_w;
		info.mipLodBias = sampler_info.mip_lod_bias;
		info.anisotropyEnable = sampler_info.anisotropy_enable;
		info.maxAnisotropy = sampler_info.max_anisotropy;
		info.compareEnable = sampler_info.compare_enable;
		info.compareOp = sampler_info.compare_op;
		info.minLod = sampler_info.min_lod;
		info.maxLod = sampler_info.max_lod;
		info.borderColor = sampler_info.border_color;
		info.unnormalizedCoordinates = sampler_info.unnormalized_coordinates;
		return info;
	}

	SamplerHandle Device::CreateSampler(const SamplerCreateInfo& sampler_info)
	{
		auto info = FillVkSamplerInfo(sampler_info);
		VkSampler sampler;
		if (table->vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
			return SamplerHandle(nullptr);
		return SamplerHandle(handle_pool.samplers.allocate(this, sampler, sampler_info));
	}

	////////////////////////////////////////
	//Image Creation////////////////////////
	////////////////////////////////////////

	static inline VkImageViewType GetImageViewType(const ImageCreateInfo& create_info, const ImageViewCreateInfo* view)
	{
		unsigned layers = view ? view->layers : create_info.layers;
		unsigned base_layer = view ? view->base_layer : 0;

		if (layers == VK_REMAINING_ARRAY_LAYERS)
			layers = create_info.layers - base_layer;

		bool force_array = view ? (view->misc & IMAGE_VIEW_MISC_FORCE_ARRAY_BIT) : (create_info.misc & IMAGE_MISC_FORCE_ARRAY_BIT);

		switch (create_info.type)
		{
		case VK_IMAGE_TYPE_1D:
			VK_ASSERT(create_info.width >= 1);
			VK_ASSERT(create_info.height == 1);
			VK_ASSERT(create_info.depth == 1);
			VK_ASSERT(create_info.samples == VK_SAMPLE_COUNT_1_BIT);

			if (layers > 1 || force_array)
				return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
			else
				return VK_IMAGE_VIEW_TYPE_1D;

		case VK_IMAGE_TYPE_2D:
			VK_ASSERT(create_info.width >= 1);
			VK_ASSERT(create_info.height >= 1);
			VK_ASSERT(create_info.depth == 1);

			if ((create_info.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) && (layers % 6) == 0)
			{
				VK_ASSERT(create_info.width == create_info.height);

				if (layers > 6 || force_array)
					return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
				else
					return VK_IMAGE_VIEW_TYPE_CUBE;
			}
			else
			{
				if (layers > 1 || force_array)
					return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
				else
					return VK_IMAGE_VIEW_TYPE_2D;
			}

		case VK_IMAGE_TYPE_3D:
			VK_ASSERT(create_info.width >= 1);
			VK_ASSERT(create_info.height >= 1);
			VK_ASSERT(create_info.depth >= 1);
			return VK_IMAGE_VIEW_TYPE_3D;

		default:
			VK_ASSERT(0 && "bogus");
			return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
		}
	}

	class ImageResourceHolder
	{
	public:
		explicit ImageResourceHolder(Device* device_)
			: device(device_)
			, table(device_->GetDeviceTable())
		{
		}

		~ImageResourceHolder()
		{
			if (owned)
				CleanUp();
		}

		Device* device;
		const VolkDeviceTable& table;

		VkImage image = VK_NULL_HANDLE;
		DeviceAllocation allocation;

		VkImageView image_view = VK_NULL_HANDLE;
		VkImageView depth_view = VK_NULL_HANDLE;
		VkImageView stencil_view = VK_NULL_HANDLE;
		VkImageView unorm_view = VK_NULL_HANDLE;
		VkImageView srgb_view = VK_NULL_HANDLE;
		VkImageViewType default_view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
		std::vector<VkImageView> rt_views;
		DeviceAllocator* allocator = nullptr;
		bool owned = true;

		VkImageViewType GetDefaltViewType() const
		{
			return default_view_type;
		}

		bool CreateDefaultViews(const ImageCreateInfo& create_info, const VkImageViewCreateInfo* view_info, bool create_unorm_srgb_views = false, const VkFormat* view_formats = nullptr)
		{
			VkDevice vkdevice = device->GetDevice();

			if ((create_info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) == 0)
			{
				QM_LOG_ERROR("Cannot create image view unless certain usage flags are present.\n");
				return false;
			}

			VkImageViewCreateInfo default_view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

			if (!view_info)
			{
				default_view_info.image = image;
				default_view_info.format = create_info.format;
				default_view_info.components = create_info.swizzle;
				default_view_info.subresourceRange.aspectMask = FormatToAspectMask(default_view_info.format);
				default_view_info.viewType = GetImageViewType(create_info, nullptr);
				default_view_info.subresourceRange.baseMipLevel = 0;
				default_view_info.subresourceRange.baseArrayLayer = 0;
				default_view_info.subresourceRange.levelCount = create_info.levels;
				default_view_info.subresourceRange.layerCount = create_info.layers;

				default_view_type = default_view_info.viewType;
			}
			else
				default_view_info = *view_info;

			view_info = &default_view_info;

			if (!CreateAltViews(create_info, *view_info))
				return false;

			if (!CreateRenderTargetViews(create_info, *view_info))
				return false;

			if (!CreateDefaultView(*view_info))
				return false;

			if (create_unorm_srgb_views)
			{
				auto info = *view_info;

				info.format = view_formats[0];
				if (table.vkCreateImageView(vkdevice, &info, nullptr, &unorm_view) != VK_SUCCESS)
					return false;

				info.format = view_formats[1];
				if (table.vkCreateImageView(vkdevice, &info, nullptr, &srgb_view) != VK_SUCCESS)
					return false;
			}

			return true;
		}

	private:

		bool CreateRenderTargetViews(const ImageCreateInfo& image_create_info, const VkImageViewCreateInfo& info)
		{
			rt_views.reserve(info.subresourceRange.layerCount);

			if (info.viewType == VK_IMAGE_VIEW_TYPE_3D)
				return true;

			// If we have a render target, and non-trivial case (layers = 1, levels = 1),
			// create an array of render targets which correspond to each layer (mip 0).
			if ((image_create_info.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0 && ((info.subresourceRange.levelCount > 1) || (info.subresourceRange.layerCount > 1)))
			{
				auto view_info = info;
				view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
				view_info.subresourceRange.baseMipLevel = info.subresourceRange.baseMipLevel;
				for (uint32_t layer = 0; layer < info.subresourceRange.layerCount; layer++)
				{
					view_info.subresourceRange.levelCount = 1;
					view_info.subresourceRange.layerCount = 1;
					view_info.subresourceRange.baseArrayLayer = layer + info.subresourceRange.baseArrayLayer;

					VkImageView rt_view;
					if (table.vkCreateImageView(device->GetDevice(), &view_info, nullptr, &rt_view) != VK_SUCCESS)
						return false;

					rt_views.push_back(rt_view);
				}
			}

			return true;
		}

		bool CreateAltViews(const ImageCreateInfo& image_create_info, const VkImageViewCreateInfo& info)
		{
			if (info.viewType == VK_IMAGE_VIEW_TYPE_CUBE || info.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY || info.viewType == VK_IMAGE_VIEW_TYPE_3D)
			{
				return true;
			}

			VkDevice vkdevice = device->GetDevice();

			if (info.subresourceRange.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
			{
				if ((image_create_info.usage & ~VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
				{
					// Sanity check. Don't want to implement layered views for this.
					if (info.subresourceRange.levelCount > 1)
					{
						QM_LOG_ERROR("Cannot create depth stencil attachments with more than 1 mip level currently, and non-DS usage flags.\n");
						return false;
					}

					if (info.subresourceRange.layerCount > 1)
					{
						QM_LOG_ERROR("Cannot create layered depth stencil attachments with non-DS usage flags.\n");
						return false;
					}

					auto view_info = info;

					// We need this to be able to sample the texture, or otherwise use it as a non-pure DS attachment.
					view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
					if (table.vkCreateImageView(vkdevice, &view_info, nullptr, &depth_view) != VK_SUCCESS)
						return false;

					view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
					if (table.vkCreateImageView(vkdevice, &view_info, nullptr, &stencil_view) != VK_SUCCESS)
						return false;
				}
			}

			return true;
		}

		bool CreateDefaultView(const VkImageViewCreateInfo& info)
		{
			VkDevice vkdevice = device->GetDevice();

			// Create the normal image view. This one contains every subresource.
			if (table.vkCreateImageView(vkdevice, &info, nullptr, &image_view) != VK_SUCCESS)
				return false;

			return true;
		}

		void CleanUp()
		{
			VkDevice vkdevice = device->GetDevice();

			if (image_view)
				table.vkDestroyImageView(vkdevice, image_view, nullptr);
			if (depth_view)
				table.vkDestroyImageView(vkdevice, depth_view, nullptr);
			if (stencil_view)
				table.vkDestroyImageView(vkdevice, stencil_view, nullptr);
			if (unorm_view)
				table.vkDestroyImageView(vkdevice, unorm_view, nullptr);
			if (srgb_view)
				table.vkDestroyImageView(vkdevice, srgb_view, nullptr);
			for (auto& view : rt_views)
				table.vkDestroyImageView(vkdevice, view, nullptr);

			if (image)
				device->managers.memory.FreeImage(image, allocation);
		}
	};

	ImageViewHandle Device::CreateImageView(const ImageViewCreateInfo& create_info)
	{
		ImageResourceHolder holder(this);
		auto& image_create_info = create_info.image->GetCreateInfo();

		VkFormat format = create_info.format != VK_FORMAT_UNDEFINED ? create_info.format : image_create_info.format;

		VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view_info.image = create_info.image->GetImage();
		view_info.format = format;
		view_info.components = create_info.swizzle;
		view_info.subresourceRange.aspectMask = FormatToAspectMask(format);
		view_info.subresourceRange.baseMipLevel = create_info.base_level;
		view_info.subresourceRange.baseArrayLayer = create_info.base_layer;
		view_info.subresourceRange.levelCount = create_info.levels;
		view_info.subresourceRange.layerCount = create_info.layers;

		if (create_info.view_type == VK_IMAGE_VIEW_TYPE_MAX_ENUM)
			view_info.viewType = GetImageViewType(image_create_info, &create_info);
		else
			view_info.viewType = create_info.view_type;

		unsigned num_levels;
		if (view_info.subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS)
			num_levels = create_info.image->GetCreateInfo().levels - view_info.subresourceRange.baseMipLevel;
		else
			num_levels = view_info.subresourceRange.levelCount;

		unsigned num_layers;
		if (view_info.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS)
			num_layers = create_info.image->GetCreateInfo().layers - view_info.subresourceRange.baseArrayLayer;
		else
			num_layers = view_info.subresourceRange.layerCount;

		view_info.subresourceRange.levelCount = num_levels;
		view_info.subresourceRange.layerCount = num_layers;

		if (!holder.CreateDefaultViews(image_create_info, &view_info))
			return ImageViewHandle(nullptr);

		ImageViewCreateInfo tmp = create_info;
		tmp.format = format;
		ImageViewHandle ret(handle_pool.image_views.allocate(this, holder.image_view, tmp));
		if (ret)
		{
			holder.owned = false;
			ret->SetAltViews(holder.depth_view, holder.stencil_view);
			ret->SetRenderTargetViews(move(holder.rt_views));
			return ret;
		}
		else
			return ImageViewHandle(nullptr);
	}

	InitialImageBuffer Device::CreateImageStagingBuffer(const TextureFormatLayout& layout)
	{
		InitialImageBuffer result;

		BufferCreateInfo buffer_info = {};
		buffer_info.domain = BufferDomain::Host;
		buffer_info.size = layout.get_required_size();
		buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		result.buffer = CreateBuffer(buffer_info);

		auto* mapped = static_cast<uint8_t*>(MapHostBuffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT));
		memcpy(mapped, layout.data(), layout.get_required_size());
		UnmapHostBuffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT);

		layout.build_buffer_image_copies(result.blits);
		return result;
	}

	InitialImageBuffer Device::CreateImageStagingBuffer(const ImageCreateInfo& info, const ImageInitialData* initial)
	{
		InitialImageBuffer result;

		bool generate_mips = (info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;
		TextureFormatLayout layout;

		unsigned copy_levels;
		if (generate_mips)
			copy_levels = 1;
		else if (info.levels == 0)
			copy_levels = TextureFormatLayout::num_miplevels(info.width, info.height, info.depth);
		else
			copy_levels = info.levels;

		switch (info.type)
		{
		case VK_IMAGE_TYPE_1D:
			layout.set_1d(info.format, info.width, info.layers, copy_levels);
			break;
		case VK_IMAGE_TYPE_2D:
			layout.set_2d(info.format, info.width, info.height, info.layers, copy_levels);
			break;
		case VK_IMAGE_TYPE_3D:
			layout.set_3d(info.format, info.width, info.height, info.depth, copy_levels);
			break;
		default:
			return {};
		}

		BufferCreateInfo buffer_info = {};
		buffer_info.domain = BufferDomain::Host;
		buffer_info.size = layout.get_required_size();
		buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		result.buffer = CreateBuffer(buffer_info);

		// And now, do the actual copy.
		auto* mapped = static_cast<uint8_t*>(MapHostBuffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT));
		unsigned index = 0;

		layout.set_buffer(mapped, layout.get_required_size());

		for (unsigned layer = 0; layer < info.layers; layer++)
		{
			ImageInitialData layer_data = initial[layer];
			
			for (unsigned level = 0; level < copy_levels; level++)
			{
				const auto& mip_info = layout.get_mip_info(level);
				uint32_t dst_height_stride = layout.get_layer_size(level);
				size_t row_size = layout.get_row_size(level);

				uint32_t src_row_length = mip_info.row_length;
				uint32_t src_array_height = mip_info.image_height;

				uint32_t src_row_stride = layout.row_byte_stride(src_row_length);
				uint32_t src_height_stride = layout.layer_byte_stride(src_array_height, src_row_stride);

				uint8_t* dst = static_cast<uint8_t*>(layout.data(layer, level));
				const uint8_t* src = static_cast<const uint8_t*>(layer_data.data);

				if (src == nullptr)
					break;

				for (uint32_t z = 0; z < mip_info.depth; z++)
					for (uint32_t y = 0; y < mip_info.block_image_height; y++)
						memcpy(dst + z * dst_height_stride + y * row_size, src + z * src_height_stride + y * src_row_stride, row_size);

				if (layer_data.next_mip)
					layer_data = *layer_data.next_mip;
				else
					break;
			}
		}

		UnmapHostBuffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT);
		layout.build_buffer_image_copies(result.blits);
		return result;
	}

	ImageHandle Device::CreateImage(const ImageCreateInfo& create_info, ResourceQueueOwnershipFlags ownership, const ImageInitialData* initial)
	{
		if (initial)
		{
			auto staging_buffer = CreateImageStagingBuffer(create_info, initial);
			return CreateImageFromStagingBuffer(create_info, ownership, &staging_buffer);
		}
		else
			return CreateImageFromStagingBuffer(create_info, ownership, nullptr);
	}

	ImageHandle Device::CreateImageFromStagingBuffer(const ImageCreateInfo& create_info, ResourceQueueOwnershipFlags ownership, const InitialImageBuffer* staging_buffer)
	{
		VK_ASSERT(ownership);

		bool is_exclusive = ownership & (RESOURCE_EXCLUSIVE_GENERIC | RESOURCE_EXCLUSIVE_ASYNC_COMPUTE | RESOURCE_EXCLUSIVE_ASYNC_GRAPHICS | RESOURCE_EXCLUSIVE_ASYNC_TRANSFER);
		bool is_concurrent = ownership & (RESOURCE_CONCURRENT_GENERIC | RESOURCE_CONCURRENT_ASYNC_COMPUTE | RESOURCE_CONCURRENT_ASYNC_GRAPHICS | RESOURCE_CONCURRENT_ASYNC_TRANSFER);

		uint32_t exclusive_target_queue_index = 0;
		CommandBuffer::Type exclusive_owner;
		if (ownership & RESOURCE_EXCLUSIVE_GENERIC)
		{
			exclusive_target_queue_index = graphics_queue_family_index;
			exclusive_owner = CommandBuffer::Type::Generic;
		}
		else if (ownership & RESOURCE_EXCLUSIVE_ASYNC_GRAPHICS)
		{
			exclusive_target_queue_index = graphics_queue_family_index;
			exclusive_owner = CommandBuffer::Type::AsyncGraphics;
		}
		else if (ownership & RESOURCE_EXCLUSIVE_ASYNC_COMPUTE)
		{
			exclusive_target_queue_index = compute_queue_family_index;
			exclusive_owner = CommandBuffer::Type::AsyncCompute;
		}
		else if (ownership & RESOURCE_EXCLUSIVE_ASYNC_TRANSFER)
		{
			exclusive_target_queue_index = transfer_queue_family_index;
			exclusive_owner = CommandBuffer::Type::AsyncTransfer;
		}

		VK_ASSERT(!(is_exclusive && is_concurrent));

		bool is_async_graphics_on_compute_queue = GetPhysicalQueueType(CommandBuffer::Type::AsyncGraphics) == CommandBuffer::Type::AsyncCompute;

		bool is_concurrent_graphics = (ownership & RESOURCE_CONCURRENT_GENERIC) || (!is_async_graphics_on_compute_queue && (ownership & RESOURCE_CONCURRENT_ASYNC_GRAPHICS));
		bool is_concurrent_compute = (ownership & RESOURCE_CONCURRENT_ASYNC_COMPUTE) || (is_async_graphics_on_compute_queue && (ownership & RESOURCE_CONCURRENT_ASYNC_GRAPHICS));
		bool is_concurrent_transfer = ownership & RESOURCE_CONCURRENT_ASYNC_TRANSFER;

		ImageResourceHolder holder(this);

		VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		info.format = create_info.format;
		info.extent.width = create_info.width;
		info.extent.height = create_info.height;
		info.extent.depth = create_info.depth;
		info.imageType = create_info.type;
		info.mipLevels = create_info.levels;
		info.arrayLayers = create_info.layers;
		info.samples = create_info.samples;

		if (create_info.domain == ImageDomain::LinearHostCached || create_info.domain == ImageDomain::LinearHost)
		{
			info.tiling = VK_IMAGE_TILING_LINEAR;
			info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		}
		else
		{
			info.tiling = VK_IMAGE_TILING_OPTIMAL;
			info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		}

		info.usage = create_info.usage;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (create_info.domain == ImageDomain::Transient)
			info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
		if (staging_buffer)
			info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		info.flags = create_info.flags;

		if (info.mipLevels == 0)
			info.mipLevels = ImageNumMipLevels(info.extent);

		VkImageFormatListCreateInfoKHR format_info = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO_KHR };
		VkFormat view_formats[2];
		format_info.pViewFormats = view_formats;
		format_info.viewFormatCount = 2;
		bool create_unorm_srgb_views = false;

		if (create_info.misc & IMAGE_MISC_MUTABLE_SRGB_BIT)
		{
			format_info.viewFormatCount = ImageCreateInfo::ComputeViewFormats(create_info, view_formats);
			if (format_info.viewFormatCount != 0)
			{
				create_unorm_srgb_views = true;
				if (ext->supports_image_format_list)
					info.pNext = &format_info;
			}
		}

		if ((create_info.usage & VK_IMAGE_USAGE_STORAGE_BIT) || (create_info.misc & IMAGE_MISC_MUTABLE_SRGB_BIT))
		{
			info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
		}

		// Only do this conditionally.
		// On AMD, using CONCURRENT with async compute disables compression.
		uint32_t sharing_indices[3] = {};

		if (is_exclusive)
		{
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			info.pQueueFamilyIndices = nullptr;
			info.queueFamilyIndexCount = 0;
		}
		else
		{
			uint32_t queueFamilyCount = 0;
			const auto add_unique_family = [&](uint32_t family) {
				for (uint32_t i = 0; i < queueFamilyCount; i++)
				{
					if (sharing_indices[i] == family)
						return;
				}
				sharing_indices[queueFamilyCount++] = family;
			};

			if (ownership & RESOURCE_CONCURRENT_GENERIC)
				add_unique_family(graphics_queue_family_index);
			if (ownership & RESOURCE_CONCURRENT_ASYNC_GRAPHICS)
				add_unique_family(is_async_graphics_on_compute_queue ? compute_queue_family_index : graphics_queue_family_index);
			if (ownership & RESOURCE_CONCURRENT_ASYNC_COMPUTE)
				add_unique_family(compute_queue_family_index);
			if (staging_buffer || (ownership & RESOURCE_CONCURRENT_ASYNC_TRANSFER) != 0)
				add_unique_family(transfer_queue_family_index);

			if (queueFamilyCount > 1)
			{
				info.sharingMode = VK_SHARING_MODE_CONCURRENT;
				info.pQueueFamilyIndices = sharing_indices;
				info.queueFamilyIndexCount = queueFamilyCount;
			}
			else
			{
				info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
				info.pQueueFamilyIndices = nullptr;
				info.queueFamilyIndexCount = 0;
			}
		}

		VkFormatFeatureFlags check_extra_features = 0;
		if ((create_info.misc & IMAGE_MISC_VERIFY_FORMAT_FEATURE_SAMPLED_LINEAR_FILTER_BIT) != 0)
			check_extra_features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

		if (info.tiling == VK_IMAGE_TILING_LINEAR)
		{
			if (staging_buffer)
				return ImageHandle(nullptr);

			// Do some more stringent checks.
			if (info.mipLevels > 1)
				return ImageHandle(nullptr);
			if (info.arrayLayers > 1)
				return ImageHandle(nullptr);
			if (info.imageType != VK_IMAGE_TYPE_2D)
				return ImageHandle(nullptr);
			if (info.samples != VK_SAMPLE_COUNT_1_BIT)
				return ImageHandle(nullptr);

			VkImageFormatProperties props;
			if (!GetImageFormatProperties(info.format, info.imageType, info.tiling, info.usage, info.flags, &props))
				return ImageHandle(nullptr);

			if (!props.maxArrayLayers ||
				!props.maxMipLevels ||
				(info.extent.width > props.maxExtent.width) ||
				(info.extent.height > props.maxExtent.height) ||
				(info.extent.depth > props.maxExtent.depth))
			{
				return ImageHandle(nullptr);
			}
		}

		if (!ImageFormatIsSupported(create_info.format, ImageUsageToFeatures(info.usage) | check_extra_features, info.tiling))
		{
			QM_LOG_ERROR("Format %u is not supported for usage flags!\n", unsigned(create_info.format));
			return ImageHandle(nullptr);
		}

		VmaAllocationCreateInfo alloc_info{};

		if (create_info.domain == ImageDomain::Physical)
		{
			alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		}
		else if (create_info.domain == ImageDomain::Transient)
		{
			alloc_info.usage = VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			alloc_info.preferredFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
		}
		else if (create_info.domain == ImageDomain::LinearHost)
		{
			alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		}
		else if (create_info.domain == ImageDomain::LinearHostCached)
		{
			alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			alloc_info.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			alloc_info.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		}

		if (!managers.memory.AllocateImage(info, alloc_info, &holder.image, &holder.allocation))
		{
			if (create_info.domain == ImageDomain::Transient)
			{
				QM_LOG_ERROR("Transient image creation failed.\n");
				return ImageHandle{ nullptr };
			}
			else
			{
				QM_LOG_ERROR("Image creation failed.\n");
				return ImageHandle{ nullptr };
			}
		}

		if (table->vkCreateImage(device, &info, nullptr, &holder.image) != VK_SUCCESS)
		{
			QM_LOG_ERROR("Failed to create image in vkCreateImage.\n");
			return ImageHandle(nullptr);
		}

		auto tmpinfo = create_info;
		tmpinfo.usage = info.usage;
		tmpinfo.flags = info.flags;
		tmpinfo.levels = info.mipLevels;

		bool has_view = (info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) != 0;

		VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
		if (has_view)
		{
			if (!holder.CreateDefaultViews(tmpinfo, nullptr, create_unorm_srgb_views, view_formats))
				return ImageHandle(nullptr);
			view_type = holder.GetDefaltViewType();
		}

		ImageHandle handle(handle_pool.images.allocate(this, holder.image, holder.image_view, holder.allocation, tmpinfo, view_type));
		if (handle)
		{
			holder.owned = false;
			if (has_view)
			{
				handle->GetView().SetAltViews(holder.depth_view, holder.stencil_view);
				handle->GetView().SetRenderTargetViews(move(holder.rt_views));
				handle->GetView().SetUnormView(holder.unorm_view);
				handle->GetView().SetSrgbView(holder.srgb_view);
			}

			// Set possible dstStage and dstAccess.
			handle->SetStageFlags(ImageUsageToPossibleStages(info.usage));
			handle->SetAccessFlags(ImageUsageToPossibleAccess(info.usage));
		}

		// Copy initial data to texture.
		if (staging_buffer)
		{
			VK_ASSERT(create_info.domain != ImageDomain::Transient);
			VK_ASSERT(create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED);
			bool generate_mips = (create_info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;

			// If graphics_queue != transfer_queue, we will use a semaphore, so no srcAccess mask is necessary.
			//VkAccessFlags final_transition_src_access = 0;
			//if (generate_mips)
			//	final_transition_src_access = VK_ACCESS_TRANSFER_READ_BIT; // Validation complains otherwise.
			//else if (graphics_queue == transfer_queue)
			//	final_transition_src_access = VK_ACCESS_TRANSFER_WRITE_BIT;

			//VkAccessFlags prepare_src_access = graphics_queue == transfer_queue ? VK_ACCESS_TRANSFER_WRITE_BIT : 0;

			// Now we've used the TRANSFER queue to copy data over to the GPU.
			// For mipmapping, we're now moving over to graphics,
			// the transfer queue is designed for CPU <-> GPU and that's it.

			// For concurrent queue mode, we just need to inject a semaphore.
			// For non-concurrent queue mode, we will have to inject ownership transfer barrier if the queue families do not match.

			VkPipelineStageFlags possible_image_stages = handle->GetStageFlags(); 
			VkAccessFlags possible_image_access = handle->GetAccessFlags() & ImageLayoutToPossibleAccess(create_info.initial_layout);

			if (is_concurrent)
			{
				auto transfer_cmd = RequestCommandBuffer(CommandBuffer::Type::AsyncTransfer);

				transfer_cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_ACCESS_TRANSFER_WRITE_BIT);

				transfer_cmd->CopyBufferToImage(*handle, *staging_buffer->buffer, staging_buffer->blits.size(), staging_buffer->blits.data());

				if (generate_mips)
				{
					if (transfer_queue == graphics_queue)
					{
						transfer_cmd->BarrierPrepareGenerateMipmap(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, true);
						transfer_cmd->GenerateMipmap(*handle);
						transfer_cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, create_info.initial_layout, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, possible_image_stages, possible_image_access);

						bool compute_sem_needed = compute_queue != transfer_queue && is_concurrent_compute;
						bool graphics_sem_needed = graphics_queue != transfer_queue && is_concurrent_graphics;

						if (compute_sem_needed && !graphics_sem_needed)
						{
							Semaphore sem[1];
							Submit(transfer_cmd, nullptr, 1, sem);
							AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
						}
						else if (!compute_sem_needed && graphics_sem_needed)
						{
							Semaphore sem[1];
							Submit(transfer_cmd, nullptr, 1, sem);
							AddWaitSemaphore(CommandBuffer::Type::Generic, sem[0], possible_image_stages, true);
						}
						else if (compute_sem_needed && graphics_sem_needed)
						{
							Semaphore sem[2];
							Submit(transfer_cmd, nullptr, 2, sem);
							AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, false);
							AddWaitSemaphore(CommandBuffer::Type::Generic, sem[1], possible_image_stages, true);
						}
						else
						{
							Submit(transfer_cmd);
						}
					}
					else
					{
						Semaphore sem;
						Submit(transfer_cmd, nullptr, 1, &sem);
						AddWaitSemaphore(CommandBuffer::Type::Generic, sem, VK_PIPELINE_STAGE_TRANSFER_BIT, true);

						auto graphics_cmd = RequestCommandBuffer(CommandBuffer::Type::Generic);

						graphics_cmd->BarrierPrepareGenerateMipmap(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, true);
						graphics_cmd->GenerateMipmap(*handle);
						graphics_cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, create_info.initial_layout, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, possible_image_stages, possible_image_access);

						bool compute_sem_needed = compute_queue != graphics_queue && is_concurrent_compute;
						bool transfer_sem_needed = transfer_queue != graphics_queue && is_concurrent_transfer;

						if (compute_sem_needed && !transfer_sem_needed)
						{

							Semaphore sem[1];
							Submit(graphics_cmd, nullptr, 1, sem);
							AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
						}
						else if (!compute_sem_needed && transfer_sem_needed)
						{
							Semaphore sem[1];
							Submit(graphics_cmd, nullptr, 1, sem);
							AddWaitSemaphore(CommandBuffer::Type::AsyncTransfer, sem[0], VK_PIPELINE_STAGE_TRANSFER_BIT, true);
						}
						else if (compute_sem_needed && transfer_sem_needed)
						{
							Semaphore sem[2];
							Submit(graphics_cmd, nullptr, 2, sem);
							AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, false);
							AddWaitSemaphore(CommandBuffer::Type::AsyncTransfer, sem[1], VK_PIPELINE_STAGE_TRANSFER_BIT, true);
						}
						else
						{
							Submit(graphics_cmd);
						}
					}
				}
				else
				{
					transfer_cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, create_info.initial_layout, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, possible_image_stages, possible_image_access);

					bool compute_sem_needed = compute_queue != transfer_queue && is_concurrent_compute;
					bool graphics_sem_needed = graphics_queue != transfer_queue && is_concurrent_graphics;

					if (compute_sem_needed && !graphics_sem_needed)
					{
						Semaphore sem[1];
						Submit(transfer_cmd, nullptr, 1, sem);
						AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
					}
					else if (!compute_sem_needed && graphics_sem_needed)
					{
						Semaphore sem[1];
						Submit(transfer_cmd, nullptr, 1, sem);
						AddWaitSemaphore(CommandBuffer::Type::Generic, sem[0], possible_image_stages, true);
					}
					else if (compute_sem_needed && graphics_sem_needed)
					{
						Semaphore sem[2];
						Submit(transfer_cmd, nullptr, 2, sem);
						AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, false);
						AddWaitSemaphore(CommandBuffer::Type::Generic, sem[1], possible_image_stages, true);
					}
					else
					{
						Submit(transfer_cmd);
					}
				}
			}
			else
			{
				
				if (exclusive_target_queue_index == graphics_queue_family_index)
				{ // No barrier needed between graphics and target

					if (graphics_queue == transfer_queue)
					{ // No barrier needed, everything can be done on one queue

						auto cmd = RequestCommandBuffer(CommandBuffer::Type::Generic);

						cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
							VK_ACCESS_TRANSFER_WRITE_BIT);

						cmd->CopyBufferToImage(*handle, *staging_buffer->buffer, staging_buffer->blits.size(), staging_buffer->blits.data());

						if (generate_mips)
						{
							cmd->BarrierPrepareGenerateMipmap(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, true);
							cmd->GenerateMipmap(*handle);
							cmd->ImageBarrier(*handle,
								VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, create_info.initial_layout,
								VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
								possible_image_stages, possible_image_access);
						}
						else
						{
							cmd->ImageBarrier(*handle,
								VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, create_info.initial_layout,
								VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
								possible_image_stages, possible_image_access);
						}

						Submit(cmd);
					}
					else
					{ // One barrier needed between transfer and graphics

						VkPipelineStageFlags dst_stages = generate_mips ? VK_PIPELINE_STAGE_TRANSFER_BIT : possible_image_stages;

						CommandBufferHandle graphics_cmd = RequestCommandBuffer(CommandBuffer::Type::Generic);
						CommandBufferHandle transfer_cmd = RequestCommandBuffer(CommandBuffer::Type::AsyncTransfer);

						transfer_cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
							VK_ACCESS_TRANSFER_WRITE_BIT);

						transfer_cmd->CopyBufferToImage(*handle, *staging_buffer->buffer, staging_buffer->blits.size(), staging_buffer->blits.data());

						VkImageMemoryBarrier release = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
						release.image = handle->GetImage();
						release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
						release.dstAccessMask = 0;
						release.srcQueueFamilyIndex = transfer_queue_family_index;
						release.dstQueueFamilyIndex = graphics_queue_family_index;
						release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

						if (generate_mips)
						{
							release.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
							release.subresourceRange.levelCount = 1;
						}
						else
						{
							release.newLayout = create_info.initial_layout;
							release.subresourceRange.levelCount = info.mipLevels;
						}

						release.subresourceRange.aspectMask = FormatToAspectMask(info.format);
						release.subresourceRange.layerCount = info.arrayLayers;

						transfer_cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
							VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
							0, nullptr, 0, nullptr, 1, &release);

						Semaphore sem;
						Submit(transfer_cmd, nullptr, 1, &sem);
						AddWaitSemaphore(CommandBuffer::Type::Generic, sem, dst_stages, true);

						VkImageMemoryBarrier acquire = release;
						acquire.srcAccessMask = 0;
						acquire.dstAccessMask = generate_mips ? VK_ACCESS_TRANSFER_READ_BIT : possible_image_access;

						graphics_cmd->Barrier(dst_stages, dst_stages, 0, nullptr, 0, nullptr, 1, &acquire);

						if (generate_mips)
						{
							graphics_cmd->BarrierPrepareGenerateMipmap(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, false);
							graphics_cmd->GenerateMipmap(*handle);
							graphics_cmd->ImageBarrier(*handle,
								VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, create_info.initial_layout,
								VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
								possible_image_stages, possible_image_access);
						}

						Submit(graphics_cmd);
					}

					
				}
				else if(exclusive_target_queue_index != graphics_queue_family_index)
				{ // Barrier needed between graphics and target

					if (graphics_queue == transfer_queue)
					{ // No barrier between graphics queue and transfer queue 
						auto graphics_cmd = RequestCommandBuffer(CommandBuffer::Type::Generic);
						auto target_cmd = RequestCommandBuffer(exclusive_owner);

						graphics_cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
							VK_ACCESS_TRANSFER_WRITE_BIT);

						graphics_cmd->CopyBufferToImage(*handle, *staging_buffer->buffer, staging_buffer->blits.size(), staging_buffer->blits.data());

						if (generate_mips)
						{
							graphics_cmd->BarrierPrepareGenerateMipmap(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, true);
							graphics_cmd->GenerateMipmap(*handle);
						}
						

						VkImageMemoryBarrier release{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
						release.image = handle->GetImage();
						release.srcAccessMask = generate_mips ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
						release.dstAccessMask = 0;
						release.srcQueueFamilyIndex = graphics_queue_family_index;
						release.dstQueueFamilyIndex = exclusive_target_queue_index;
						release.oldLayout = generate_mips ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
						release.newLayout = create_info.initial_layout;
						release.subresourceRange.levelCount = info.mipLevels;
						release.subresourceRange.aspectMask = FormatToAspectMask(info.format);
						release.subresourceRange.layerCount = info.arrayLayers;

						graphics_cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, nullptr, 0, nullptr, 1, &release);

						Semaphore sem;
						Submit(graphics_cmd, nullptr, 1, &sem);
						AddWaitSemaphore(exclusive_owner, sem, possible_image_stages, true);

						auto target_cmd = RequestCommandBuffer(exclusive_owner);

						VkImageMemoryBarrier acquire = release;
						acquire.srcAccessMask = 0;
						acquire.dstAccessMask = possible_image_access;

						target_cmd->Barrier(possible_image_stages, possible_image_stages, 0, nullptr, 0, nullptr, 1, &acquire);

						Submit(target_cmd);

					}
					else
					{ // Two barriers needed, transfer->graphics->target
						auto transfer_cmd = RequestCommandBuffer(CommandBuffer::Type::AsyncTransfer);

						transfer_cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
							VK_ACCESS_TRANSFER_WRITE_BIT);

						transfer_cmd->CopyBufferToImage(*handle, *staging_buffer->buffer, staging_buffer->blits.size(), staging_buffer->blits.data());

						if (generate_mips)
						{
							VkImageMemoryBarrier transfer_release = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
							transfer_release.image = handle->GetImage();
							transfer_release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
							transfer_release.dstAccessMask = 0;
							transfer_release.srcQueueFamilyIndex = transfer_queue_family_index;
							transfer_release.dstQueueFamilyIndex = graphics_queue_family_index;
							transfer_release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
							transfer_release.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
							transfer_release.subresourceRange.levelCount = 1;
							transfer_release.subresourceRange.aspectMask = FormatToAspectMask(info.format);
							transfer_release.subresourceRange.layerCount = info.arrayLayers;

							transfer_cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, nullptr, 0, nullptr, 1, &transfer_release);

							Semaphore sem;
							Submit(transfer_cmd, nullptr, 1, &sem);
							AddWaitSemaphore(CommandBuffer::Type::Generic, sem, VK_PIPELINE_STAGE_TRANSFER_BIT, true);

							auto graphics_cmd = RequestCommandBuffer(CommandBuffer::Type::Generic);

							VkImageMemoryBarrier graphics_acquire = transfer_release;
							graphics_acquire.srcAccessMask = 0;
							graphics_acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

							graphics_cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0, nullptr, 1, &graphics_acquire);
							graphics_cmd->BarrierPrepareGenerateMipmap(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, false);
							graphics_cmd->GenerateMipmap(*handle);

							VkImageMemoryBarrier graphics_release = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
							graphics_release.image = handle->GetImage();
							graphics_release.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
							graphics_release.dstAccessMask = 0;
							graphics_release.srcQueueFamilyIndex = graphics_queue_family_index;
							graphics_release.dstQueueFamilyIndex = exclusive_target_queue_index;
							graphics_release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
							graphics_release.newLayout = create_info.initial_layout;
							graphics_release.subresourceRange.levelCount = info.mipLevels;
							graphics_release.subresourceRange.aspectMask = FormatToAspectMask(info.format);
							graphics_release.subresourceRange.layerCount = info.arrayLayers;

							graphics_cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, nullptr, 0, nullptr, 1, &transfer_release);

							Semaphore sem;
							Submit(graphics_cmd, nullptr, 1, &sem);
							AddWaitSemaphore(exclusive_owner, sem, possible_image_stages, true);

							auto target_cmd = RequestCommandBuffer(exclusive_owner);

							VkImageMemoryBarrier target_acquire = graphics_release;
							target_acquire.srcAccessMask = 0;
							target_acquire.dstAccessMask = possible_image_access;

							target_cmd->Barrier(possible_image_stages, possible_image_stages, 0, nullptr, 0, nullptr, 1, &target_acquire);

							Submit(target_cmd);
						}
						else
						{
							if (exclusive_owner == CommandBuffer::Type::AsyncTransfer)
							{
								transfer_cmd->ImageBarrier(*handle,
									VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, create_info.initial_layout,
									VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
									possible_image_stages, possible_image_access);

								Submit(transfer_cmd);
							}
							else
							{
								VkImageMemoryBarrier transfer_release = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
								transfer_release.image = handle->GetImage();
								transfer_release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
								transfer_release.dstAccessMask = 0;
								transfer_release.srcQueueFamilyIndex = transfer_queue_family_index;
								transfer_release.dstQueueFamilyIndex = exclusive_target_queue_index;
								transfer_release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
								transfer_release.newLayout = create_info.initial_layout;
								transfer_release.subresourceRange.levelCount = info.mipLevels;
								transfer_release.subresourceRange.aspectMask = FormatToAspectMask(info.format);
								transfer_release.subresourceRange.layerCount = info.arrayLayers;

								transfer_cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, nullptr, 0, nullptr, 1, &transfer_release);

								Semaphore sem;
								Submit(transfer_cmd, nullptr, 1, &sem);
								AddWaitSemaphore(exclusive_owner, sem, possible_image_stages, true);

								auto target_cmd = RequestCommandBuffer(exclusive_owner);

								VkImageMemoryBarrier target_acquire = transfer_release;
								target_acquire.srcAccessMask = 0;
								target_acquire.dstAccessMask = possible_image_access;

								target_cmd->Barrier(possible_image_stages, possible_image_stages, 0, nullptr, 0, nullptr, 1, &target_acquire);

								Submit(target_cmd);

							}
						}
					}
					
				}

			}
		}
		else if (create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
		{
			VK_ASSERT(create_info.domain != ImageDomain::Transient);
			auto cmd = RequestCommandBuffer(CommandBuffer::Type::Generic);
			cmd->ImageBarrier(*handle, info.initialLayout, create_info.initial_layout, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, handle->GetStageFlags(), handle->GetAccessFlags() & ImageLayoutToPossibleAccess(create_info.initial_layout));

			bool compute_sem_needed = compute_queue != graphics_queue && is_concurrent_compute;
			bool transfer_sem_needed = transfer_queue != graphics_queue && is_concurrent_transfer;

			if (compute_sem_needed && !transfer_sem_needed)
			{
				Semaphore sem[1];
				Submit(cmd, nullptr, 1, sem);
				AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
			}
			else if (!compute_sem_needed && transfer_sem_needed)
			{
				Semaphore sem[1];
				Submit(cmd, nullptr, 1, sem);
				AddWaitSemaphore(CommandBuffer::Type::AsyncTransfer, sem[0], VK_PIPELINE_STAGE_TRANSFER_BIT, true);
			}
			else if (compute_sem_needed && transfer_sem_needed)
			{
				Semaphore sem[2];
				Submit(cmd, nullptr, 2, sem);
				AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, false);
				AddWaitSemaphore(CommandBuffer::Type::AsyncTransfer, sem[1], VK_PIPELINE_STAGE_TRANSFER_BIT, true);
			}
			else
			{
				Submit(cmd);
			}
		}

		return handle;
	}
	
	/////////////////////////////
	//Bindless descriptors///////
	////////////////////////////

	/*BindlessDescriptorPoolHandle Device::CreateBindlessDescriptorPool(BindlessResourceType type,
		unsigned num_sets, unsigned num_descriptors)
	{
		if (!ext->supports_descriptor_indexing)
			return BindlessDescriptorPoolHandle{ nullptr };

		DescriptorSetAllocator* allocator = nullptr;

		switch (type)
		{
		case BindlessResourceType::ImageFP:
			allocator = bindless_sampled_image_allocator_fp;
			break;

		case BindlessResourceType::ImageInt:
			allocator = bindless_sampled_image_allocator_integer;
			break;

		default:
			break;
		}

		VkDescriptorPool pool = VK_NULL_HANDLE;
		if (allocator)
			pool = allocator->AllocateBindlessPool(num_sets, num_descriptors);

		if (!pool)
		{
			QM_LOG_ERROR("Failed to allocate bindless pool.\n");
			return BindlessDescriptorPoolHandle{ nullptr };
		}

		auto* handle = handle_pool.bindless_descriptor_pool.allocate(this, allocator, pool);
		return BindlessDescriptorPoolHandle{ handle };
	}*/

	////////////////////////////////////////
	//Helper functions//////////////////////
	////////////////////////////////////////

	void Device::GetFormatProperties(VkFormat format, VkFormatProperties* properties)
	{
		vkGetPhysicalDeviceFormatProperties(gpu, format, properties);
	}

	bool Device::GetImageFormatProperties(VkFormat format, VkImageType type, VkImageTiling tiling,
		VkImageUsageFlags usage, VkImageCreateFlags flags,
		VkImageFormatProperties* properties)
	{
		auto res = vkGetPhysicalDeviceImageFormatProperties(gpu, format, type, tiling, usage, flags,
			properties);
		return res == VK_SUCCESS;
	}

	bool Device::ImageFormatIsSupported(VkFormat format, VkFormatFeatureFlags required, VkImageTiling tiling) const
	{
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
		auto flags = tiling == VK_IMAGE_TILING_OPTIMAL ? props.optimalTilingFeatures : props.linearTilingFeatures;
		return (flags & required) == required;
	}

	VkFormat Device::GetDefaultDepthStencilFormat() const
	{
		if (ImageFormatIsSupported(VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
			return VK_FORMAT_D24_UNORM_S8_UINT;
		if (ImageFormatIsSupported(VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
			return VK_FORMAT_D32_SFLOAT_S8_UINT;

		return VK_FORMAT_UNDEFINED;
	}

	VkFormat Device::GetDefaultDepthFormat() const
	{
		if (ImageFormatIsSupported(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
			return VK_FORMAT_D32_SFLOAT;
		if (ImageFormatIsSupported(VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
			return VK_FORMAT_X8_D24_UNORM_PACK32;
		if (ImageFormatIsSupported(VK_FORMAT_D16_UNORM, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL))
			return VK_FORMAT_D16_UNORM;

		return VK_FORMAT_UNDEFINED;
	}

	uint64_t Device::AllocateCookie()
	{
		// Reserve lower bits for "special purposes".
#ifdef QM_VULKAN_MT
		return cookie.fetch_add(16, memory_order_relaxed) + 16;
#else
		cookie += 16;
		return cookie;
#endif
	}

	///////////////////////////////
	//Renderpass Hashing///////////
	///////////////////////////////


	const RenderPass& Device::RequestRenderPass(const RenderPassInfo& info, bool compatible)
	{
		Hasher h;
		VkFormat formats[VULKAN_NUM_ATTACHMENTS];
		VkFormat depth_stencil;
		uint32_t lazy = 0;
		uint32_t optimal = 0;

		for (unsigned i = 0; i < info.num_color_attachments; i++)
		{
			VK_ASSERT(info.color_attachments[i]);
			formats[i] = info.color_attachments[i]->GetFormat();
			if (info.color_attachments[i]->GetImage().GetCreateInfo().domain == ImageDomain::Transient)
				lazy |= 1u << i;
			if (info.color_attachments[i]->GetImage().GetLayoutType() == Layout::Optimal)
				optimal |= 1u << i;

			// This can change external subpass dependencies, so it must always be hashed.
			h.u32(info.color_attachments[i]->GetImage().GetSwapchainLayout());
		}

		if (info.depth_stencil)
		{
			if (info.depth_stencil->GetImage().GetCreateInfo().domain == ImageDomain::Transient)
				lazy |= 1u << info.num_color_attachments;
			if (info.depth_stencil->GetImage().GetLayoutType() == Layout::Optimal)
				optimal |= 1u << info.num_color_attachments;
		}

		// For multiview, base layer is encoded into the view mask.
		if (info.num_layers > 1)
		{
			h.u32(info.base_layer);
			h.u32(info.num_layers);
		}
		else
		{
			h.u32(0);
			h.u32(info.num_layers);
		}

		h.u32(info.num_subpasses);
		for (unsigned i = 0; i < info.num_subpasses; i++)
		{
			h.u32(info.subpasses[i].num_color_attachments);
			h.u32(info.subpasses[i].num_input_attachments);
			h.u32(info.subpasses[i].num_resolve_attachments);
			h.u32(static_cast<uint32_t>(info.subpasses[i].depth_stencil_mode));
			for (unsigned j = 0; j < info.subpasses[i].num_color_attachments; j++)
				h.u32(info.subpasses[i].color_attachments[j]);
			for (unsigned j = 0; j < info.subpasses[i].num_input_attachments; j++)
				h.u32(info.subpasses[i].input_attachments[j]);
			for (unsigned j = 0; j < info.subpasses[i].num_resolve_attachments; j++)
				h.u32(info.subpasses[i].resolve_attachments[j]);
		}

		depth_stencil = info.depth_stencil ? info.depth_stencil->GetFormat() : VK_FORMAT_UNDEFINED;
		h.data(formats, info.num_color_attachments * sizeof(VkFormat));
		h.u32(info.num_color_attachments);
		h.u32(depth_stencil);

		// Compatible render passes do not care about load/store, or image layouts.
		if (!compatible)
		{
			h.u32(info.op_flags);
			h.u32(info.clear_attachments);
			h.u32(info.load_attachments);
			h.u32(info.store_attachments);
			h.u32(optimal);
		}

		// Lazy flag can change external subpass dependencies, which is not compatible.
		h.u32(lazy);

		auto hash = h.get();

		auto* ret = render_passes.find(hash);
		if (!ret)
			ret = render_passes.emplace_yield(hash, hash, this, info);
		return *ret;
	}

	const Framebuffer& Device::RequestFramebuffer(const RenderPassInfo& info)
	{
		return framebuffer_allocator.request_framebuffer(info);
	}

	ImageView& Device::GetTransientAttachment(unsigned width, unsigned height, VkFormat format,
		unsigned index, unsigned samples, unsigned layers)
	{
		return transient_allocator.request_attachment(width, height, format, index, samples, layers);
	}

	ImageView& Device::GetPhysicalAttachment(unsigned width, unsigned height, VkFormat format,
		unsigned index, unsigned samples, unsigned layers)
	{
		return physical_allocator.request_attachment(width, height, format, index, samples, layers);
	}

	ImageView& Device::GetSwapchainView()
	{
		VK_ASSERT(wsi.index < wsi.swapchain.size());
		return wsi.swapchain[wsi.index]->GetView();
	}

	ImageView& Device::GetSwapchainView(unsigned index)
	{
		VK_ASSERT(index < wsi.swapchain.size());
		return wsi.swapchain[index]->GetView();
	}

	unsigned Device::GetNumFrameContexts() const
	{
		return unsigned(per_frame.size());
	}

	unsigned Device::GetNumSwapchainImages() const
	{
		return unsigned(wsi.swapchain.size());
	}

	unsigned Device::GetSwapchainIndex() const
	{
		return wsi.index;
	}

	unsigned Device::GetCurrentFrameContext() const
	{
		return frame_context_index;
	}

	RenderPassInfo Device::GetSwapchainRenderPass(SwapchainRenderPass style)
	{
		RenderPassInfo info;
		info.num_color_attachments = 1;
		info.color_attachments[0] = &GetSwapchainView();
		info.clear_attachments = ~0u;
		info.store_attachments = 1u << 0;

		switch (style)
		{
		case SwapchainRenderPass::Depth:
		{
			info.op_flags |= RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
			info.depth_stencil = &GetTransientAttachment(wsi.swapchain[wsi.index]->GetCreateInfo().width, wsi.swapchain[wsi.index]->GetCreateInfo().height, GetDefaultDepthFormat());
			break;
		}

		case SwapchainRenderPass::DepthStencil:
		{
			info.op_flags |= RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
			info.depth_stencil = &GetTransientAttachment(wsi.swapchain[wsi.index]->GetCreateInfo().width, wsi.swapchain[wsi.index]->GetCreateInfo().height, GetDefaultDepthStencilFormat());
			break;
		}

		default:
			break;
		}
		return info;
	}

	void Device::SetQueueLock(std::function<void()> lock_callback, std::function<void()> unlock_callback)
	{
		queue_lock_callback = move(lock_callback);
		queue_unlock_callback = move(unlock_callback);
	}

}