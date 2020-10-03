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
static unsigned GetThreadIndex()
{
	return Vulkan::GetCurrentThreadIndex();
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
static unsigned GetThreadIndex()
{
	return 0;
}
#endif

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
				pool.RecycleBlock(std::move(block));
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

		auto cmd = RequestCommandBufferNolock(GetThreadIndex(), CommandBuffer::Type::AsyncTransfer);

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
		UpdateInvalidProgramsNoLock();

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
		wsi.acquire = std::move(acquire);
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
		auto ret = std::move(wsi.release);
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
			auto frame = std::unique_ptr<PerFrame>(new PerFrame(this, i));
			per_frame.emplace_back(std::move(frame));
		}
	}

	void Device::InitExternalSwapchain(const std::vector<ImageHandle>& swapchain_images)
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

	void Device::InitSwapchain(const std::vector<VkImage>& swapchain_images, unsigned width, unsigned height, VkFormat format)
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
		Frame().keep_alive_images.push_back(std::move(handle));
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
			managers.vbo.RecycleBlock(std::move(block));
		for (auto& block : ibo_blocks)
			managers.ibo.RecycleBlock(std::move(block));
		for (auto& block : ubo_blocks)
			managers.ubo.RecycleBlock(std::move(block));
		for (auto& block : staging_blocks)
			managers.staging.RecycleBlock(std::move(block));

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
		return cookie.fetch_add(16, std::memory_order_relaxed) + 16;
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