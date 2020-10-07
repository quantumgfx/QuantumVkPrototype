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

	PipelineEvent Device::RequestPipelineEvent()
	{
		return PipelineEvent(handle_pool.events.allocate(this, managers.event.RequestClearedEvent()));
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
		return framebuffer_allocator.RequestFramebuffer(info);
	}

	ImageView& Device::GetTransientAttachment(uint32_t width, uint32_t height, VkFormat format,
		uint32_t index, VkSampleCountFlagBits samples, uint32_t layers)
	{
		return transient_allocator.RequestAttachment(width, height, format, index, samples, layers);
	}

	ImageView& Device::GetPhysicalAttachment(uint32_t width, uint32_t height, VkFormat format,
		uint32_t index, VkSampleCountFlagBits samples, uint32_t layers)
	{
		return physical_allocator.RequestAttachment(width, height, format, index, samples, layers);
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

	uint32_t Device::GetSwapchainWidth() const
	{
		return wsi.swapchain[wsi.index]->GetCreateInfo().width;
	}

	uint32_t Device::GetSwapchainHeight() const
	{
		return wsi.swapchain[wsi.index]->GetCreateInfo().height;
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
			info.depth_stencil = &GetTransientAttachment(GetSwapchainWidth(), GetSwapchainHeight(), GetDefaultDepthFormat());
			break;
		}

		case SwapchainRenderPass::DepthStencil:
		{
			info.op_flags |= RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT;
			info.depth_stencil = &GetTransientAttachment(GetSwapchainWidth(), GetSwapchainHeight(), GetDefaultDepthStencilFormat());
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