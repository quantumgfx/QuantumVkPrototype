#include "device.hpp"
#include "format.hpp"
#include "type_to_string.hpp"
#include "quirks.hpp"
#include "utils/timer.hpp"
#include <algorithm>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

#ifdef QM_VULKAN_MT
#include "threading/thread_id.hpp"
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
		
	{
#ifdef QM_VULKAN_MT
		cookie.store(0);
#endif


	}

	Semaphore Device::RequestLegacySemaphore()
	{
		LOCK();
		//Gets a cleared semaphore
		auto semaphore = managers.semaphore.request_cleared_semaphore();
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

		semaphore->signal_pending_wait();
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
		auto gpu_image = CreateImage(create_info);
		if (!gpu_image)
		{
			// Fall-back to staging buffer.
			create_info.domain = ImageDomain::Physical;
			create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			create_info.misc = IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT | IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT;
			create_info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			gpu_image = CreateImage(create_info);
			if (!gpu_image)
				return LinearHostImageHandle(nullptr);

			BufferCreateInfo buffer;
			buffer.domain = (info.flags & LINEAR_HOST_IMAGE_HOST_CACHED_BIT) != 0 ? BufferDomain::CachedHost : BufferDomain::Host;
			buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			buffer.size = info.width * info.height * TextureFormatLayout::format_block_size(info.format, format_to_aspect_mask(info.format));
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

	void* Device::MapHostBuffer(const Buffer& buffer, MemoryAccessFlags access)
	{
		void* host = managers.memory.MapMemory(buffer.GetAllocation(), access);
		return host;
	}

	void Device::UnmapHostBuffer(const Buffer& buffer, MemoryAccessFlags access)
	{
		managers.memory.UnmapMemory(buffer.GetAllocation(), access);
	}

	Shader* Device::RequestShader(const uint32_t* data, size_t size)
	{
		//Generates a hash of the data
		Util::Hasher hasher;
		hasher.data(data, size);
		//If hash already exists just return that shader
		auto hash = hasher.get();
		auto* ret = shaders.find(hash);
		//Else construct a new shader before returning it
		if (!ret)
			ret = shaders.emplace_yield(hash, hash, this, data, size);
		return ret;
	}

	Shader* Device::RequestShaderByHash(Hash hash)
	{
		return shaders.find(hash);
	}

	Program* Device::RequestProgram(Vulkan::Shader* compute_shader)
	{
		if (!compute_shader)
			return nullptr;

		Util::Hasher hasher;
		//Get program hash
		hasher.u64(compute_shader->get_hash());

		auto hash = hasher.get();
		//Check if program already exists
		auto* ret = programs.find(hash);
		if (!ret) //If not construct a new program
			ret = programs.emplace_yield(hash, this, compute_shader);
		return ret;
	}

	Program* Device::RequestProgram(const uint32_t* compute_data, size_t compute_size)
	{
		if (!compute_size)
			return nullptr;

		auto* compute_shader = RequestShader(compute_data, compute_size);
		return RequestProgram(compute_shader);
	}

	Program* Device::RequestProgram(Shader* vertex, Shader* fragment)
	{
		if (!vertex || !fragment)
			return nullptr;

		Util::Hasher hasher;
		hasher.u64(vertex->get_hash());
		hasher.u64(fragment->get_hash());

		auto hash = hasher.get();
		auto* ret = programs.find(hash);

		if (!ret)
			ret = programs.emplace_yield(hash, this, vertex, fragment);
		return ret;
	}

	Program* Device::RequestProgram(const uint32_t* vertex_data, size_t vertex_size, const uint32_t* fragment_data, size_t fragment_size)
	{
		if (!vertex_size || !fragment_size)
			return nullptr;

		auto* vertex = RequestShader(vertex_data, vertex_size);
		auto* fragment = RequestShader(fragment_data, fragment_size);
		return RequestProgram(vertex, fragment);
	}

	PipelineLayout* Device::RequestPipelineLayout(const CombinedResourceLayout& layout)
	{
		Hasher h;
		h.data(reinterpret_cast<const uint32_t*>(layout.sets), sizeof(layout.sets));
		h.data(&layout.stages_for_bindings[0][0], sizeof(layout.stages_for_bindings));
		h.u32(layout.push_constant_range.stageFlags);
		h.u32(layout.push_constant_range.size);
		h.data(layout.spec_constant_mask, sizeof(layout.spec_constant_mask));
		h.u32(layout.attribute_mask);
		h.u32(layout.render_target_mask);

		auto hash = h.get();
		// 
		auto* ret = pipeline_layouts.find(hash);
		if (!ret)
			ret = pipeline_layouts.emplace_yield(hash, hash, this, layout);
		return ret;
	}

	DescriptorSetAllocator* Device::RequestDescriptorSetAllocator(const DescriptorSetLayout& layout, const uint32_t* stages_for_bindings)
	{
		Hasher h;
		h.data(reinterpret_cast<const uint32_t*>(&layout), sizeof(layout));
		h.data(stages_for_bindings, sizeof(uint32_t) * VULKAN_NUM_BINDINGS);
		auto hash = h.get();

		auto* ret = descriptor_set_allocators.find(hash);
		if (!ret)
			ret = descriptor_set_allocators.emplace_yield(hash, hash, this, layout, stages_for_bindings);
		return ret;
	}

	void Device::BakeProgram(Program& program)
	{
		CombinedResourceLayout layout;
		if (program.GetShader(ShaderStage::Vertex))
			layout.attribute_mask = program.GetShader(ShaderStage::Vertex)->GetLayout().input_mask;
		if (program.GetShader(ShaderStage::Fragment))
			layout.render_target_mask = program.GetShader(ShaderStage::Fragment)->GetLayout().output_mask;

		layout.descriptor_set_mask = 0;

		for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
		{
			auto* shader = program.GetShader(static_cast<ShaderStage>(i));
			if (!shader)
				continue;

			uint32_t stage_mask = 1u << i;

			auto& shader_layout = shader->GetLayout();
			for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
			{
				layout.sets[set].sampled_image_mask |= shader_layout.sets[set].sampled_image_mask;
				layout.sets[set].storage_image_mask |= shader_layout.sets[set].storage_image_mask;
				layout.sets[set].uniform_buffer_mask |= shader_layout.sets[set].uniform_buffer_mask;
				layout.sets[set].storage_buffer_mask |= shader_layout.sets[set].storage_buffer_mask;
				layout.sets[set].sampled_buffer_mask |= shader_layout.sets[set].sampled_buffer_mask;
				layout.sets[set].input_attachment_mask |= shader_layout.sets[set].input_attachment_mask;
				layout.sets[set].sampler_mask |= shader_layout.sets[set].sampler_mask;
				layout.sets[set].separate_image_mask |= shader_layout.sets[set].separate_image_mask;
				layout.sets[set].fp_mask |= shader_layout.sets[set].fp_mask;

				for_each_bit(shader_layout.sets[set].immutable_sampler_mask, [&](uint32_t binding) {
					StockSampler sampler = GetImmutableSampler(shader_layout.sets[set], binding);

					// Do we already have an immutable sampler? Make sure it matches the layout.
					if (HasImmutableSampler(layout.sets[set], binding))
					{
						if (sampler != GetImmutableSampler(layout.sets[set], binding))
							QM_LOG_ERROR("Immutable sampler mismatch detected!\n");
					}

					SetImmutableSampler(layout.sets[set], binding, sampler);
					});

				uint32_t active_binds =
					shader_layout.sets[set].sampled_image_mask |
					shader_layout.sets[set].storage_image_mask |
					shader_layout.sets[set].uniform_buffer_mask |
					shader_layout.sets[set].storage_buffer_mask |
					shader_layout.sets[set].sampled_buffer_mask |
					shader_layout.sets[set].input_attachment_mask |
					shader_layout.sets[set].sampler_mask |
					shader_layout.sets[set].separate_image_mask;

				if (active_binds)
					layout.stages_for_sets[set] |= stage_mask;

				for_each_bit(active_binds, [&](uint32_t bit) {
					layout.stages_for_bindings[set][bit] |= stage_mask;

					auto& combined_size = layout.sets[set].array_size[bit];
					auto& shader_size = shader_layout.sets[set].array_size[bit];
					if (combined_size && combined_size != shader_size)
						QM_LOG_ERROR("Mismatch between array sizes in different shaders.\n");
					else
						combined_size = shader_size;
					});
			}

			// Merge push constant ranges into one range.
			// Do not try to split into multiple ranges as it just complicates things for no obvious gain.
			if (shader_layout.push_constant_size != 0)
			{
				layout.push_constant_range.stageFlags |= 1u << i;
				layout.push_constant_range.size =
					std::max(layout.push_constant_range.size, shader_layout.push_constant_size);
			}

			layout.spec_constant_mask[i] = shader_layout.spec_constant_mask;
			layout.combined_spec_constant_mask |= shader_layout.spec_constant_mask;
			layout.bindless_descriptor_set_mask |= shader_layout.bindless_set_mask;
		}

		for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
		{
			if (layout.stages_for_sets[set] != 0)
			{
				layout.descriptor_set_mask |= 1u << set;

				for (unsigned binding = 0; binding < VULKAN_NUM_BINDINGS; binding++)
				{
					auto& array_size = layout.sets[set].array_size[binding];
					if (array_size == DescriptorSetLayout::UNSIZED_ARRAY)
					{
						for (unsigned i = 1; i < VULKAN_NUM_BINDINGS; i++)
						{
							if (layout.stages_for_bindings[set][i] != 0)
								QM_LOG_ERROR("Using bindless for set = %u, but binding = %u has a descriptor attached to it.\n", set, i);
						}

						// Allows us to have one unified descriptor set layout for bindless.
						layout.stages_for_bindings[set][binding] = VK_SHADER_STAGE_ALL;
					}
					else if (array_size == 0)
					{
						array_size = 1;
					}
					else
					{
						for (unsigned i = 1; i < array_size; i++)
						{
							if (layout.stages_for_bindings[set][binding + i] != 0)
							{
								QM_LOG_ERROR("Detected binding aliasing for (%u, %u). Binding array with %u elements starting at (%u, %u) overlaps.\n",
									set, binding + i, array_size, set, binding);
							}
						}
					}
				}
			}
		}

		Hasher h;
		h.u32(layout.push_constant_range.stageFlags);
		h.u32(layout.push_constant_range.size);
		layout.push_constant_layout_hash = h.get();
		program.SetPipelineLayout(RequestPipelineLayout(layout));
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

	Util::RetainedHeapData Vulkan::Device::GetPipelineCacheData(size_t override_max_size)
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

		uint8_t* data = new uint8_t[max_size];

		if (table->vkGetPipelineCacheData(device, pipeline_cache, &max_size, data) != VK_SUCCESS)
		{
			QM_LOG_ERROR("Failed to get pipeline cache data.\n");
		}

		RetainedHeapData heap_data = CreateRetainedHeapData(data, max_size);

		delete[] data;

		return heap_data;
	}

	void Device::SetContext(const ContextHandle& context_, uint8_t* initial_cache_data, size_t initiale_cache_size, uint8_t* fossilize_pipeline_data, size_t fossilize_pipeline_size)
	{
		context = context_;
		table = &context_->GetDeviceTable();
		ext = &context_->GetEnabledDeviceFeatures();

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
		InitBindless();

#ifdef ANDROID
		InitFrameContexts(3); // Android needs a bit more ... ;)
#else
		InitFrameContexts(2); // By default, regular double buffer between CPU and GPU.
#endif

		managers.memory.Init(this);
		managers.semaphore.init(this);
		managers.fence.init(this);
		managers.event.init(this);
		managers.vbo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ImplementationQuirks::get().staging_need_device_local);
		managers.ibo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, ImplementationQuirks::get().staging_need_device_local);
		managers.ubo.init(this, 256 * 1024, std::max<VkDeviceSize>(16u, gpu_props.limits.minUniformBufferOffsetAlignment), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ImplementationQuirks::get().staging_need_device_local);
		managers.ubo.set_spill_region_size(VULKAN_MAX_UBO_SIZE);
		managers.staging.init(this, 64 * 1024, std::max<VkDeviceSize>(16u, gpu_props.limits.optimalBufferCopyOffsetAlignment),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			false);

		graphics.performance_query_pool.init_device(this, graphics_queue_family_index);
		if (graphics_queue_family_index != compute_queue_family_index)
			compute.performance_query_pool.init_device(this, compute_queue_family_index);
		if (graphics_queue_family_index != transfer_queue_family_index &&
			compute_queue_family_index != transfer_queue_family_index)
		{
			transfer.performance_query_pool.init_device(this, transfer_queue_family_index);
		}

		InitPipelineCache(initial_cache_data, initiale_cache_size);
		
		InitFossilizePipeline(fossilize_pipeline_data, fossilize_pipeline_size);

#ifdef QM_VULKAN_FILESYSTEM
		init_shader_manager_cache();
#endif
	}

	void Device::InitBindless()
	{
		if (!ext->supports_descriptor_indexing)
			return;

		DescriptorSetLayout layout;

		layout.array_size[0] = DescriptorSetLayout::UNSIZED_ARRAY;
		for (unsigned i = 1; i < VULKAN_NUM_BINDINGS; i++)
			layout.array_size[i] = 1;

		layout.separate_image_mask = 1;
		uint32_t stages_for_sets[VULKAN_NUM_BINDINGS] = { VK_SHADER_STAGE_ALL };
		bindless_sampled_image_allocator_integer = RequestDescriptorSetAllocator(layout, stages_for_sets);
		layout.fp_mask = 1;
		bindless_sampled_image_allocator_fp = RequestDescriptorSetAllocator(layout, stages_for_sets);
	}

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

			samplers[i] = CreateSampler(info, mode);
		}
	}

	static void RequestBlock(Device& device, BufferBlock& block, VkDeviceSize size, BufferPool& pool, std::vector<BufferBlock>* dma, std::vector<BufferBlock>& recycle)
	{
		if (block.mapped)
			device.UnmapHostBuffer(*block.cpu, MEMORY_ACCESS_WRITE_BIT);

		if (block.offset == 0)
		{
			if (block.size == pool.get_block_size())
				pool.recycle_block(move(block));
		}
		else
		{
			if (block.cpu != block.gpu)
			{
				VK_ASSERT(dma);
				dma->push_back(block);
			}

			if (block.size == pool.get_block_size())
				recycle.push_back(block);
		}

		if (size)
			block = pool.request_block(size);
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

	void Device::Submit(CommandBufferHandle& cmd, Fence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		//Lock mutex
		LOCK();
		SubmitNolock(move(cmd), fence, semaphore_count, semaphores);
	}

	CommandBuffer::Type Device::GetPhysicalQueueType(CommandBuffer::Type queue_type) const
	{
		if (queue_type != CommandBuffer::Type::AsyncGraphics)
		{
			return queue_type;
		}
		else
		{
			if (graphics_queue_family_index == compute_queue_family_index && graphics_queue != compute_queue)
				return CommandBuffer::Type::AsyncCompute;
			else
				return CommandBuffer::Type::Generic;
		}
	}

	void Device::SubmitNolock(CommandBufferHandle cmd, Fence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		//Get the command buffer type
		auto type = cmd->GetCommandBufferType();
		auto& submissions = GetQueueSubmission(type);
#ifdef VULKAN_DEBUG
		auto& pool = GetCommandPool(type, cmd->GetThreadIndex());
		pool.SignalSubmitted(cmd->GetCommandBuffer());
#endif

		//End command buffer
		cmd->End();
		submissions.push_back(move(cmd));

		InternalFence signalled_fence;

		if (fence || semaphore_count)
		{
			SubmitQueue(type, fence ? &signalled_fence : nullptr, semaphore_count, semaphores);
		}

		if (fence)
		{
			VK_ASSERT(!*fence);
			if (signalled_fence.value)
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.value, signalled_fence.timeline));
			else
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.fence));
		}

		DecrementFrameCounterNolock();
	}

	void Device::SubmitEmpty(CommandBuffer::Type type, Fence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		LOCK();
		SubmitEmptyNolock(type, fence, semaphore_count, semaphores);
	}

	void Device::SubmitEmptyNolock(CommandBuffer::Type type, Fence* fence,
		unsigned semaphore_count, Semaphore* semaphores)
	{
		if (type != CommandBuffer::Type::AsyncTransfer)
			FlushFrame(CommandBuffer::Type::AsyncTransfer);

		InternalFence signalled_fence;
		SubmitQueue(type, fence ? &signalled_fence : nullptr, semaphore_count, semaphores);
		if (fence)
		{
			if (signalled_fence.value)
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.value, signalled_fence.timeline));
			else
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.fence));
		}
	}

	void Device::SubmitEmptyInner(CommandBuffer::Type type, InternalFence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		auto& data = GetQueueData(type);
		VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		VkTimelineSemaphoreSubmitInfoKHR timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

		if (ext->timeline_semaphore_features.timelineSemaphore)
			submit.pNext = &timeline_info;

		VkSemaphore timeline_semaphore = data.timeline_semaphore;
		uint64_t timeline_value = ++data.current_timeline;

		VkQueue queue = GetVkQueue(type);
		switch (type)
		{
		default:
		case CommandBuffer::Type::Generic:
			Frame().timeline_fence_graphics = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			if (ext->timeline_semaphore_features.timelineSemaphore)
			{
				QM_LOG_INFO("Signal graphics: (%p) %u\n",
					reinterpret_cast<void*>(timeline_semaphore),
					unsigned(data.current_timeline));
			}
#endif
			break;

		case CommandBuffer::Type::AsyncCompute:
			Frame().timeline_fence_compute = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			if (ext->timeline_semaphore_features.timelineSemaphore)
			{
				LOGI("Signal compute: (%p) %u\n",
					reinterpret_cast<void*>(timeline_semaphore),
					unsigned(data.current_timeline));
			}
#endif
			break;

		case CommandBuffer::Type::AsyncTransfer:
			Frame().timeline_fence_transfer = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			if (ext.timeline_semaphore_features.timelineSemaphore)
			{
				LOGI("Signal transfer: (%p) %u\n",
					reinterpret_cast<void*>(timeline_semaphore),
					unsigned(data.current_timeline));
			}
#endif
			break;
		}

		// Add external signal semaphores.
		SmallVector<VkSemaphore> signals;
		if (ext->timeline_semaphore_features.timelineSemaphore)
		{
			// Signal once and distribute the timeline value to all.
			timeline_info.signalSemaphoreValueCount = 1;
			timeline_info.pSignalSemaphoreValues = &timeline_value;
			submit.signalSemaphoreCount = 1;
			submit.pSignalSemaphores = &timeline_semaphore;

			if (fence)
			{
				fence->timeline = timeline_semaphore;
				fence->value = timeline_value;
				fence->fence = VK_NULL_HANDLE;
			}

			for (unsigned i = 0; i < semaphore_count; i++)
			{
				VK_ASSERT(!semaphores[i]);
				semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, timeline_value, timeline_semaphore));
			}
		}
		else
		{
			if (fence)
			{
				fence->timeline = VK_NULL_HANDLE;
				fence->value = 0;
			}

			for (unsigned i = 0; i < semaphore_count; i++)
			{
				VkSemaphore cleared_semaphore = managers.semaphore.request_cleared_semaphore();
				signals.push_back(cleared_semaphore);
				VK_ASSERT(!semaphores[i]);
				semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, cleared_semaphore, true));
			}

			submit.signalSemaphoreCount = signals.size();
			if (!signals.empty())
				submit.pSignalSemaphores = signals.data();
		}

		// Add external wait semaphores.
		SmallVector<VkSemaphore> waits;
		SmallVector<uint64_t> waits_count;
		auto stages = move(data.wait_stages);

		for (auto& semaphore : data.wait_semaphores)
		{
			auto wait = semaphore->consume();
			if (!semaphore->get_timeline_value())
			{
				if (semaphore->can_recycle())
					Frame().recycled_semaphores.push_back(wait);
				else
					Frame().destroyed_semaphores.push_back(wait);
			}
			waits.push_back(wait);
			waits_count.push_back(semaphore->get_timeline_value());
		}

		data.wait_stages.clear();
		data.wait_semaphores.clear();

		submit.waitSemaphoreCount = waits.size();
		if (!stages.empty())
			submit.pWaitDstStageMask = stages.data();
		if (!waits.empty())
			submit.pWaitSemaphores = waits.data();

		if (!waits_count.empty())
		{
			timeline_info.waitSemaphoreValueCount = waits_count.size();
			timeline_info.pWaitSemaphoreValues = waits_count.data();
		}

		VkFence cleared_fence = fence && !ext->timeline_semaphore_features.timelineSemaphore ? managers.fence.request_cleared_fence() : VK_NULL_HANDLE;
		if (fence)
			fence->fence = cleared_fence;

		if (queue_lock_callback)
			queue_lock_callback();
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		if (cleared_fence)
			LOGI("Signalling Fence: %llx\n", reinterpret_cast<unsigned long long>(cleared_fence));
#endif

		VkResult result = table->vkQueueSubmit(queue, 1, &submit, cleared_fence);
		if (ImplementationQuirks::get().queue_wait_on_submission)
			table->vkQueueWaitIdle(queue);
		if (queue_unlock_callback)
			queue_unlock_callback();

		if (result != VK_SUCCESS)
			QM_LOG_ERROR("vkQueueSubmit failed (code: %d).\n", int(result));

		if (!ext->timeline_semaphore_features.timelineSemaphore)
			data.need_fence = true;

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		const char* queue_name = nullptr;
		switch (type)
		{
		default:
		case CommandBuffer::Type::Generic:
			queue_name = "Graphics";
			break;
		case CommandBuffer::Type::AsyncCompute:
			queue_name = "Compute";
			break;
		case CommandBuffer::Type::AsyncTransfer:
			queue_name = "Transfer";
			break;
		}

		QM_LOG_INFO("Empty submission to %s queue:\n", queue_name);
		for (uint32_t i = 0; i < submit.waitSemaphoreCount; i++)
		{
			QM_LOG_INFO("  Waiting for semaphore: %llx in stages %s\n",
				reinterpret_cast<unsigned long long>(submit.pWaitSemaphores[i]),
				stage_flags_to_string(submit.pWaitDstStageMask[i]).c_str());
		}

		for (uint32_t i = 0; i < submit.signalSemaphoreCount; i++)
		{
			QM_LOG_INFO("  Signalling semaphore: %llx\n",
				reinterpret_cast<unsigned long long>(submit.pSignalSemaphores[i]));
		}
#endif
	}

	Fence Device::RequestLegacyFence()
	{
		VkFence fence = managers.fence.request_cleared_fence();
		return Fence(handle_pool.fences.allocate(this, fence));
	}

	void Device::SubmitStaging(CommandBufferHandle& cmd, VkBufferUsageFlags usage, bool flush)
	{
		auto access = BufferUsageToPossibleAccess(usage);
		auto stages = BufferUsageToPossibleStages(usage);
		VkQueue src_queue = GetVkQueue(cmd->GetCommandBufferType());

		if (src_queue == graphics_queue && src_queue == compute_queue)
		{ // There is only one queue. Ensure all writes to the buffer are finished and then submit it normally.
			// For single-queue systems, just use a pipeline barrier.
			cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, stages, access);
			SubmitNolock(cmd, nullptr, 0, nullptr);
		}
		else
		{
			auto compute_stages = stages & (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);

			auto compute_access = access & (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

			auto graphics_stages = stages;

			if (src_queue == graphics_queue)
			{
				// Make sure all writes are finished and visible
				cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, graphics_stages, access);

				if (compute_stages != 0)
				{
					// Submit is and make sure all graphics submissions are finished before another AsyncCompute submit
					Semaphore sem;
					SubmitNolock(cmd, nullptr, 1, &sem);
					AddWaitSemaphoreNolock(CommandBuffer::Type::AsyncCompute, sem, compute_stages, flush);
				}
				else // Just submit. All other uses of the resources will be on the same queue
					SubmitNolock(cmd, nullptr, 0, nullptr);
			}
			else if (src_queue == compute_queue)
			{
				// Make sure all writes are finished and visible
				cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, compute_stages, compute_access);

				if (graphics_stages != 0)
				{
					Semaphore sem;
					SubmitNolock(cmd, nullptr, 1, &sem);
					AddWaitSemaphoreNolock(CommandBuffer::Type::Generic, sem, graphics_stages, flush);
				}
				else
					SubmitNolock(cmd, nullptr, 0, nullptr);
			}
			else
			{
				//This is running on the transfer queue. No need for a barrier as smeaphores will take care of it
				if (graphics_stages != 0 && compute_stages != 0)
				{
					Semaphore semaphores[2];
					SubmitNolock(cmd, nullptr, 2, semaphores);
					//Graphics and compute submission wait for this result
					AddWaitSemaphoreNolock(CommandBuffer::Type::Generic, semaphores[0], graphics_stages, flush);
					AddWaitSemaphoreNolock(CommandBuffer::Type::AsyncCompute, semaphores[1], compute_stages, flush);
				}
				else if (graphics_stages != 0)
				{
					Semaphore sem;
					SubmitNolock(cmd, nullptr, 1, &sem);
					//Generic submissions wait for this result
					AddWaitSemaphoreNolock(CommandBuffer::Type::Generic, sem, graphics_stages, flush);
				}
				else if (compute_stages != 0)
				{
					Semaphore sem;
					SubmitNolock(cmd, nullptr, 1, &sem);
					//Compute submissions wait for this result
					AddWaitSemaphoreNolock(CommandBuffer::Type::AsyncCompute, sem, compute_stages, flush);
				}
				else
					//Just submit
					SubmitNolock(cmd, nullptr, 0, nullptr);
			}
		}
	}

	void Device::SubmitQueue(CommandBuffer::Type type, InternalFence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		//Get queue type
		type = GetPhysicalQueueType(type);

		// Always check if we need to flush pending transfers.
		if (type != CommandBuffer::Type::AsyncTransfer)
			FlushFrame(CommandBuffer::Type::AsyncTransfer);

		auto& data = GetQueueData(type);
		auto& submissions = GetQueueSubmission(type);

		if (submissions.empty())
		{
			//If there are no submissions, but fences/semaphores depend on this submission, then submit an empty command
			if (fence || semaphore_count)
				SubmitEmptyInner(type, fence, semaphore_count, semaphores);
			return;
		}

		VkSemaphore timeline_semaphore = data.timeline_semaphore;
		uint64_t timeline_value = ++data.current_timeline;
		//Get the queue
		VkQueue queue = GetVkQueue(type);
		switch (type)
		{
		default:
		case CommandBuffer::Type::Generic:
			Frame().timeline_fence_graphics = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Signal graphics: (%p) %u\n",
				reinterpret_cast<void*>(timeline_semaphore),
				unsigned(data.current_timeline));
#endif
			break;

		case CommandBuffer::Type::AsyncCompute:
			Frame().timeline_fence_compute = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Signal compute: (%p) %u\n",
				reinterpret_cast<void*>(timeline_semaphore),
				unsigned(data.current_timeline));
#endif
			break;

		case CommandBuffer::Type::AsyncTransfer:
			Frame().timeline_fence_transfer = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Signal transfer: (%p) %u\n",
				reinterpret_cast<void*>(timeline_semaphore),
				unsigned(data.current_timeline));
#endif
			break;
		}

		//TODO persistant memory (aka just a vector in device class)

		//Commands to submit
		SmallVector<VkCommandBuffer> cmds;
		cmds.reserve(submissions.size());

		//Batched queue submits
		SmallVector<VkSubmitInfo> submits;
		SmallVector<VkTimelineSemaphoreSubmitInfoKHR> timeline_infos;

		submits.reserve(2);
		timeline_infos.reserve(2);

		size_t last_cmd = 0;

		SmallVector<VkSemaphore> waits[2];
		SmallVector<uint64_t> wait_counts[2];
		SmallVector<VkFlags> wait_stages[2];
		SmallVector<VkSemaphore> signals[2];
		SmallVector<uint64_t> signal_counts[2];

		// Add external wait semaphores.
		wait_stages[0] = std::move(data.wait_stages);

		for (auto& semaphore : data.wait_semaphores)
		{
			auto wait = semaphore->consume();
			if (!semaphore->get_timeline_value())
			{
				if (semaphore->can_recycle())
					Frame().recycled_semaphores.push_back(wait);
				else
					Frame().destroyed_semaphores.push_back(wait);
			}
			wait_counts[0].push_back(semaphore->get_timeline_value());
			waits[0].push_back(wait);
		}

		//Reset wait stages and semaphores
		data.wait_stages.clear();
		data.wait_semaphores.clear();

		for (auto& cmd : submissions)
		{
			if (cmd->SwapchainTouched() && !wsi.touched && !wsi.consumed)
			{
				// If cmd involves swapchain
				if (!cmds.empty())
				{
					// If submmission contains some commands that don't involve the swapchain

					// Push them into thier own submission.

					// Create new submission and timeline-semaphore-info
					submits.emplace_back();
					timeline_infos.emplace_back();

					//Set stype
					auto& timeline_info = timeline_infos.back();
					timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

					auto& submit = submits.back();
					submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

					//If timeline semaphores supported, set pnext
					if (ext->timeline_semaphore_features.timelineSemaphore)
						submit.pNext = &timeline_info;

					// This submission will batch the non-swapchain involving commands together
					submit.commandBufferCount = cmds.size() - last_cmd;
					submit.pCommandBuffers = cmds.data() + last_cmd;

					last_cmd = cmds.size();
				}
				//Indicate that the wsi is involved in this submission
				wsi.touched = true;
			}
			//Push command into pending submission queue
			cmds.push_back(cmd->GetCommandBuffer());
		}

		if (cmds.size() > last_cmd)
		{
			//If there are commands that weren't part of the first submit (which there will always be)

			unsigned index = submits.size();

			// Push all pending cmd buffers to their own submission.
			// Create new submission and timeline-semaphore-info
			submits.emplace_back();
			timeline_infos.emplace_back();

			//Set stype
			auto& timeline_info = timeline_infos.back();
			timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

			auto& submit = submits.back();
			submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

			// If timeline semaphores supported, set pnext
			if (ext->timeline_semaphore_features.timelineSemaphore)
				submit.pNext = &timeline_info;

			submit.commandBufferCount = cmds.size() - last_cmd;
			submit.pCommandBuffers = cmds.data() + last_cmd;

			// No need to add QueueData.wait stages/semaphores to this second submission
			// All queueSubmission begin execution in order. They just may complete out of order.

			// If the swapchain is touched and it has an aquire semaphore
			if (wsi.touched && !wsi.consumed)
			{
				static const VkFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				if (wsi.acquire && wsi.acquire->get_semaphore() != VK_NULL_HANDLE)
				{
					// Then make this submission batch (which has one or more swapchain touching commands buffers) wait for the aquire semaphore.
					// Basically this batch will wait for vkAquireNextImageKHR to complete before being submitted, as it has commands that depend on the
					// swapchain image.
					VK_ASSERT(wsi.acquire->is_signalled());
					VkSemaphore sem = wsi.acquire->consume();

					waits[index].push_back(sem);
					wait_counts[index].push_back(wsi.acquire->get_timeline_value());
					wait_stages[index].push_back(wait);

					if (!wsi.acquire->get_timeline_value())
					{
						if (wsi.acquire->can_recycle())
							Frame().recycled_semaphores.push_back(sem);
						else
							Frame().destroyed_semaphores.push_back(sem);
					}

					wsi.acquire.Reset();
				}

				VkSemaphore release = managers.semaphore.request_cleared_semaphore();
				wsi.release = Semaphore(handle_pool.semaphores.allocate(this, release, true));
				wsi.release->set_internal_sync_object();
				signals[index].push_back(wsi.release->get_semaphore());
				signal_counts[index].push_back(0);
				wsi.consumed = true;
			}
			last_cmd = cmds.size();
		}

		// In short, the algorithm above puts commands into at most two batches. The first can be submitted and work on it can start immediately.
		// While the second must wait for the aquire semaphore to finish. For example:
		// Key: N - command that doesn't touch the swapchain, S - command that involves the swapchain
		// Batch 1: (N, N, N, N, N) - The first batch doesn't ever use the swapchain, so it doesn't need to wait for it.
		// Batch 2: (S, N, S, S, N, N) - The second involves the swapchain, so it must wait for VkAquireNextImageKHR to finish.

		VkFence cleared_fence = fence && !ext->timeline_semaphore_features.timelineSemaphore ? managers.fence.request_cleared_fence() : VK_NULL_HANDLE;

		if (fence)
			fence->fence = cleared_fence;

		// Add external signal semaphores.
		if (ext->timeline_semaphore_features.timelineSemaphore)
		{
			// Signal once and distribute the timeline value to all.
			signals[submits.size() - 1].push_back(timeline_semaphore);
			signal_counts[submits.size() - 1].push_back(timeline_value);

			if (fence)
			{
				fence->timeline = timeline_semaphore;
				fence->value = timeline_value;
				fence->fence = VK_NULL_HANDLE;
			}

			for (unsigned i = 0; i < semaphore_count; i++)
			{
				VK_ASSERT(!semaphores[i]);
				semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, timeline_value, timeline_semaphore));
			}
		}
		else
		{
			if (fence)
			{
				fence->timeline = VK_NULL_HANDLE;
				fence->value = 0;
			}

			for (unsigned i = 0; i < semaphore_count; i++)
			{
				VkSemaphore cleared_semaphore = managers.semaphore.request_cleared_semaphore();
				signals[submits.size() - 1].push_back(cleared_semaphore);
				signal_counts[submits.size() - 1].push_back(0);
				VK_ASSERT(!semaphores[i]);
				semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, cleared_semaphore, true));
			}
		}

		//Gather all infomation for the submits
		for (unsigned i = 0; i < submits.size(); i++)
		{
			auto& submit = submits[i];
			auto& timeline_submit = timeline_infos[i];

			submit.waitSemaphoreCount = waits[i].size();
			submit.pWaitSemaphores = waits[i].data();
			submit.pWaitDstStageMask = wait_stages[i].data();
			timeline_submit.waitSemaphoreValueCount = submit.waitSemaphoreCount;
			timeline_submit.pWaitSemaphoreValues = wait_counts[i].data();

			submit.signalSemaphoreCount = signals[i].size();
			submit.pSignalSemaphores = signals[i].data();
			timeline_submit.signalSemaphoreValueCount = submit.signalSemaphoreCount;
			timeline_submit.pSignalSemaphoreValues = signal_counts[i].data();
		}

		if (queue_lock_callback)
			queue_lock_callback();
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		if (cleared_fence)
			QM_LOG_ERROR("Signalling fence: %llx\n", reinterpret_cast<unsigned long long>(cleared_fence));
#endif
		//Submit the command batches
		VkResult result = table->vkQueueSubmit(queue, submits.size(), submits.data(), cleared_fence);
		if (ImplementationQuirks::get().queue_wait_on_submission)
			table->vkQueueWaitIdle(queue);
		if (queue_unlock_callback)
			queue_unlock_callback();

		if (result != VK_SUCCESS)
			QM_LOG_ERROR("vkQueueSubmit failed (code: %d).\n", int(result));

		submissions.clear();

		if (!ext->timeline_semaphore_features.timelineSemaphore)
			data.need_fence = true;

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		const char* queue_name = nullptr;
		switch (type)
		{
		default:
		case CommandBuffer::Type::Generic:
			queue_name = "Graphics";
			break;
		case CommandBuffer::Type::AsyncCompute:
			queue_name = "Compute";
			break;
		case CommandBuffer::Type::AsyncTransfer:
			queue_name = "Transfer";
			break;
		}

		for (auto& submit : submits)
		{
			QM_LOG_INFO("Submission to %s queue:\n", queue_name);
			for (uint32_t i = 0; i < submit.waitSemaphoreCount; i++)
			{
				QM_LOG_INFO("  Waiting for semaphore: %llx in stages %s\n",
					reinterpret_cast<unsigned long long>(submit.pWaitSemaphores[i]),
					stage_flags_to_string(submit.pWaitDstStageMask[i]).c_str());
			}

			for (uint32_t i = 0; i < submit.commandBufferCount; i++)
				QM_LOG_INFO(" Command Buffer %llx\n", reinterpret_cast<unsigned long long>(submit.pCommandBuffers[i]));

			for (uint32_t i = 0; i < submit.signalSemaphoreCount; i++)
			{
				QM_LOG_INFO("  Signalling semaphore: %llx\n",
					reinterpret_cast<unsigned long long>(submit.pSignalSemaphores[i]));
			}
		}
#endif
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

		auto cmd = RequestCommandBufferNolock(get_thread_index(), CommandBuffer::Type::AsyncTransfer, false);

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
			image->set_internal_sync_object();
			image->GetView().set_internal_sync_object();
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

	QueueData& Device::GetQueueData(CommandBuffer::Type type)
	{
		switch (GetPhysicalQueueType(type))
		{
		default:
		case CommandBuffer::Type::Generic:
			return graphics;
		case CommandBuffer::Type::AsyncCompute:
			return compute;
		case CommandBuffer::Type::AsyncTransfer:
			return transfer;
		}
	}

	VkQueue Device::GetVkQueue(CommandBuffer::Type type) const
	{
		switch (GetPhysicalQueueType(type))
		{
		default:
		case CommandBuffer::Type::Generic:
			return graphics_queue;
		case CommandBuffer::Type::AsyncCompute:
			return compute_queue;
		case CommandBuffer::Type::AsyncTransfer:
			return transfer_queue;
		}
	}

	CommandPool& Device::GetCommandPool(CommandBuffer::Type type, unsigned thread)
	{
		switch (GetPhysicalQueueType(type))
		{
		default:
		case CommandBuffer::Type::Generic:
			return Frame().graphics_cmd_pool[thread];
		case CommandBuffer::Type::AsyncCompute:
			return Frame().compute_cmd_pool[thread];
		case CommandBuffer::Type::AsyncTransfer:
			return Frame().transfer_cmd_pool[thread];
		}
	}

	Util::SmallVector<CommandBufferHandle>& Device::GetQueueSubmission(CommandBuffer::Type type)
	{
		switch (GetPhysicalQueueType(type))
		{
		default:
		case CommandBuffer::Type::Generic:
			return Frame().graphics_submissions;
		case CommandBuffer::Type::AsyncCompute:
			return Frame().compute_submissions;
		case CommandBuffer::Type::AsyncTransfer:
			return Frame().transfer_submissions;
		}
	}

	CommandBufferHandle Device::RequestCommandBuffer(CommandBuffer::Type type)
	{
		return RequestCommandBufferForThread(get_thread_index(), type);
	}

	CommandBufferHandle Device::RequestCommandBufferForThread(unsigned thread_index, CommandBuffer::Type type)
	{
		LOCK();
		return RequestCommandBufferNolock(thread_index, type, false);
	}

	CommandBufferHandle Device::RequestCommandBufferNolock(unsigned thread_index, CommandBuffer::Type type, bool profiled)
	{
#ifndef QM_VULKAN_MT
		VK_ASSERT(thread_index == 0);
#endif
		auto cmd = GetCommandPool(type, thread_index).RequestCommandBuffer();

		VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		table->vkBeginCommandBuffer(cmd, &info);
		AddFrameCounterNolock();
		CommandBufferHandle handle(handle_pool.command_buffers.allocate(this, cmd, pipeline_cache, type));
		handle->SetThreadIndex(thread_index);

		return handle;
	}

	void Device::SubmitSecondary(CommandBuffer& primary, CommandBuffer& secondary)
	{
		{
			LOCK();
			secondary.End();
			DecrementFrameCounterNolock();

#ifdef VULKAN_DEBUG
			auto& pool = GetCommandPool(secondary.GetCommandBufferType(),
				secondary.GetThreadIndex());
			pool.SignalSubmitted(secondary.GetCommandBuffer());
#endif
		}

		VkCommandBuffer secondary_cmd = secondary.GetCommandBuffer();
		table->vkCmdExecuteCommands(primary.GetCommandBuffer(), 1, &secondary_cmd);
	}

	CommandBufferHandle Device::RequestSecondaryCommandBufferForThread(unsigned thread_index, const Framebuffer* framebuffer, unsigned subpass, CommandBuffer::Type type)
	{
		LOCK();

		auto cmd = GetCommandPool(type, thread_index).RequestSecondaryCommandBuffer();
		VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		VkCommandBufferInheritanceInfo inherit = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };

		inherit.framebuffer = VK_NULL_HANDLE;
		inherit.renderPass = framebuffer->get_compatible_render_pass().get_render_pass();
		inherit.subpass = subpass;
		info.pInheritanceInfo = &inherit;
		info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

		table->vkBeginCommandBuffer(cmd, &info);
		AddFrameCounterNolock();
		CommandBufferHandle handle(handle_pool.command_buffers.allocate(this, cmd, pipeline_cache, type));
		handle->SetThreadIndex(thread_index);
		handle->SetIsSecondary();
		return handle;
	}

	void Device::SetAcquireSemaphore(unsigned index, Semaphore acquire)
	{
		wsi.acquire = move(acquire);
		wsi.index = index;
		wsi.touched = false;
		wsi.consumed = false;

		if (wsi.acquire)
		{
			wsi.acquire->set_internal_sync_object();
			VK_ASSERT(wsi.acquire->is_signalled());
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

	/*Device::~Device()
	{
		wait_idle();

		managers.timestamps.log_simple();

		wsi.acquire.reset();
		wsi.release.reset();
		wsi.swapchain.clear();

		if (pipeline_cache != VK_NULL_HANDLE)
		{
			flush_pipeline_cache();
			table->vkDestroyPipelineCache(device, pipeline_cache, nullptr);
		}

#ifdef GRANITE_VULKAN_FILESYSTEM
		flush_shader_manager_cache();
#endif

#ifdef GRANITE_VULKAN_FOSSILIZE
		flush_pipeline_state();
#endif

		framebuffer_allocator.clear();
		transient_allocator.clear();
		for (auto& sampler : samplers)
			sampler.reset();

		for (auto& sampler : samplers_ycbcr)
			if (sampler)
				table->vkDestroySamplerYcbcrConversion(device, sampler, nullptr);

		deinit_timeline_semaphores();
	}*/

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

	void Device::InitFrameContexts(unsigned count)
	{
		DRAIN_FRAME_LOCK();
		WaitIdleNolock();

		// Clear out caches which might contain stale data from now on.
		framebuffer_allocator.clear();
		transient_allocator.clear();
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
				wsi.swapchain.back()->set_internal_sync_object();
				wsi.swapchain.back()->GetView().set_internal_sync_object();
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
			view_info.subresourceRange.aspectMask = format_to_aspect_mask(format);
			view_info.subresourceRange.baseMipLevel = 0;
			view_info.subresourceRange.baseArrayLayer = 0;
			view_info.subresourceRange.levelCount = 1;
			view_info.subresourceRange.layerCount = 1;
			view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;

			VkImageView image_view;
			if (table->vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS)
				QM_LOG_ERROR("Failed to create view for backbuffer.");

			auto backbuffer = ImageHandle(handle_pool.images.allocate(this, image, image_view, DeviceAllocation{}, info, VK_IMAGE_VIEW_TYPE_2D));
			backbuffer->set_internal_sync_object();
			backbuffer->disown_image();
			backbuffer->GetView().set_internal_sync_object();
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

	void Device::DestroyPipeline(VkPipeline pipeline)
	{
		LOCK();
		DestroyPipelineNolock(pipeline);
	}

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

	void Device::DestroyDescriptorPool(VkDescriptorPool desc_pool)
	{
		LOCK();
		DestroyDescriptorPoolNolock(desc_pool);
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

	void Device::DestroyPipelineNolock(VkPipeline pipeline)
	{
		VK_ASSERT(!exists(Frame().destroyed_pipelines, pipeline));
		Frame().destroyed_pipelines.push_back(pipeline);
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
			managers.fence.recycle_fence(fence);
		}
		else
			Frame().recycle_fences.push_back(fence);
	}

	void Device::DestroyImageNolock(VkImage image, const DeviceAllocation& allocation)
	{
		VK_ASSERT(!exists(Frame().destroyed_images, std::make_pair(image, allocation)));
		Frame().destroyed_images.push_back(std::make_pair(image, allocation));
	}

	void Device::DestroyBufferNolock(VkBuffer buffer, const DeviceAllocation& allocation)
	{
		VK_ASSERT(!exists(Frame().destroyed_buffers, std::make_pair(buffer, allocation)));
		Frame().destroyed_buffers.push_back(std::make_pair(buffer, allocation));
	}

	void Device::DestroyDescriptorPoolNolock(VkDescriptorPool desc_pool)
	{
		VK_ASSERT(!exists(Frame().destroyed_descriptor_pools, desc_pool));
		Frame().destroyed_descriptor_pools.push_back(desc_pool);
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

		if (device.GetDeviceFeatures().timeline_semaphore_features.timelineSemaphore &&
			graphics_timeline_semaphore && compute_timeline_semaphore && transfer_timeline_semaphore)
		{
			VkSemaphoreWaitInfoKHR info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR };
			const VkSemaphore semaphores[3] = { graphics_timeline_semaphore, compute_timeline_semaphore, transfer_timeline_semaphore };
			const uint64_t values[3] = { timeline_fence_graphics, timeline_fence_compute, timeline_fence_transfer };

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			if (device.get_device_features().timeline_semaphore_features.timelineSemaphore)
			{
				LOGI("Waiting for graphics (%p) %u\n",
					reinterpret_cast<void*>(graphics_timeline_semaphore),
					unsigned(timeline_fence_graphics));
				LOGI("Waiting for compute (%p) %u\n",
					reinterpret_cast<void*>(compute_timeline_semaphore),
					unsigned(timeline_fence_compute));
				LOGI("Waiting for transfer (%p) %u\n",
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
				LOGI("Waiting for Fence: %llx\n", reinterpret_cast<unsigned long long>(fence));
#endif
			table.vkWaitForFences(vkdevice, wait_fences.size(), wait_fences.data(), VK_TRUE, UINT64_MAX);
			wait_fences.clear();
		}

		// If we're using timeline semaphores, these paths should never be hit.
		if (!recycle_fences.empty())
		{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			for (auto& fence : recycle_fences)
				LOGI("Recycling Fence: %llx\n", reinterpret_cast<unsigned long long>(fence));
#endif
			table.vkResetFences(vkdevice, recycle_fences.size(), recycle_fences.data());
			for (auto& fence : recycle_fences)
				managers.fence.recycle_fence(fence);
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
		for (auto& pipeline : destroyed_pipelines)
			table.vkDestroyPipeline(vkdevice, pipeline, nullptr);
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
		for (auto& pool : destroyed_descriptor_pools)
			table.vkDestroyDescriptorPool(vkdevice, pool, nullptr);
		for (auto& semaphore : recycled_semaphores)
		{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Recycling semaphore: %llx\n", reinterpret_cast<unsigned long long>(semaphore));
#endif
			managers.semaphore.recycle(semaphore);
		}
		for (auto& event : recycled_events)
			managers.event.recycle(event);

		for (auto& block : vbo_blocks)
			managers.vbo.recycle_block(move(block));
		for (auto& block : ibo_blocks)
			managers.ibo.recycle_block(move(block));
		for (auto& block : ubo_blocks)
			managers.ubo.recycle_block(move(block));
		for (auto& block : staging_blocks)
			managers.staging.recycle_block(move(block));

		vbo_blocks.clear();
		ibo_blocks.clear();
		ubo_blocks.clear();
		staging_blocks.clear();

		destroyed_framebuffers.clear();
		destroyed_samplers.clear();
		destroyed_pipelines.clear();
		destroyed_image_views.clear();
		destroyed_buffer_views.clear();
		destroyed_images.clear();
		destroyed_buffers.clear();
		destroyed_semaphores.clear();
		destroyed_descriptor_pools.clear();
		recycled_semaphores.clear();
		recycled_events.clear();

		int64_t min_timestamp_us = std::numeric_limits<int64_t>::max();
		int64_t max_timestamp_us = 0;
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
		return PipelineEvent(handle_pool.events.allocate(this, managers.event.request_cleared_event()));
	}

	void Device::ClearWaitSemaphores()
	{
		for (auto& sem : graphics.wait_semaphores)
			table->vkDestroySemaphore(device, sem->consume(), nullptr);
		for (auto& sem : compute.wait_semaphores)
			table->vkDestroySemaphore(device, sem->consume(), nullptr);
		for (auto& sem : transfer.wait_semaphores)
			table->vkDestroySemaphore(device, sem->consume(), nullptr);

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
		managers.vbo.reset();
		managers.ubo.reset();
		managers.ibo.reset();
		managers.staging.reset();
		for (auto& frame : per_frame)
		{
			frame->vbo_blocks.clear();
			frame->ibo_blocks.clear();
			frame->ubo_blocks.clear();
			frame->staging_blocks.clear();
		}

		framebuffer_allocator.clear();
		transient_allocator.clear();
		for (auto& allocator : descriptor_set_allocators)
			allocator.clear();

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

		framebuffer_allocator.begin_frame();
		transient_allocator.begin_frame();
		for (auto& allocator : descriptor_set_allocators)
			allocator.begin_frame();

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

	BufferHandle Device::CreateBuffer(const BufferCreateInfo& create_info, const void* initial)
	{
		VkBuffer buffer;
		DeviceAllocation allocation;

		bool zero_initialize = (create_info.misc & BUFFER_MISC_ZERO_INITIALIZE_BIT) != 0;
		if (initial && zero_initialize)
		{
			QM_LOG_ERROR("Cannot initialize buffer with data and clear.\n");
			return BufferHandle{};
		}

		VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		info.size = create_info.size;
		info.usage = create_info.usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		uint32_t sharing_indices[3];
		FillBufferSharingIndices(info, sharing_indices);

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
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
		}
		else if (create_info.domain == BufferDomain::LinkedDeviceHost)
		{
			alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		}

		if(!managers.memory.AllocateBuffer(info, alloc_info, &buffer, &allocation))
			return BufferHandle(nullptr);

		
		auto tmpinfo = create_info;
		tmpinfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		BufferHandle handle(handle_pool.buffers.allocate(this, buffer, allocation, tmpinfo));

		if (create_info.domain == BufferDomain::Device && (initial || zero_initialize) && !AllocationHasMemoryPropertyFlags(allocation, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
		{
			CommandBufferHandle cmd;
			if (initial)
			{
				auto staging_info = create_info;
				staging_info.domain = BufferDomain::Host;
				auto staging_buffer = CreateBuffer(staging_info, initial);

				cmd = RequestCommandBuffer(CommandBuffer::Type::AsyncTransfer);
				cmd->CopyBuffer(*handle, *staging_buffer);
			}
			else
			{
				cmd = RequestCommandBuffer(CommandBuffer::Type::AsyncCompute);
				cmd->FillBuffer(*handle, 0);
			}

			LOCK();
			SubmitStaging(cmd, info.usage, true);
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

	SamplerHandle Device::CreateSampler(const SamplerCreateInfo& sampler_info, StockSampler stock_sampler)
	{
		auto info = FillVkSamplerInfo(sampler_info);
		VkSampler sampler;

		if (table->vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS)
			return SamplerHandle(nullptr);
#ifdef QM_VULKAN_FOSSILIZE
		register_sampler(sampler, Fossilize::Hash(stock_sampler) | 0x10000, info);
#else
		(void)stock_sampler;
#endif
		SamplerHandle handle(handle_pool.samplers.allocate(this, sampler, sampler_info));
		handle->set_internal_sync_object();
		return handle;
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
		DeviceAllocation allocation;
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
				default_view_info.subresourceRange.aspectMask = format_to_aspect_mask(default_view_info.format);
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
		view_info.subresourceRange.aspectMask = format_to_aspect_mask(format);
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
		result.buffer = CreateBuffer(buffer_info, nullptr);

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
		result.buffer = CreateBuffer(buffer_info, nullptr);

		// And now, do the actual copy.
		auto* mapped = static_cast<uint8_t*>(MapHostBuffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT));
		unsigned index = 0;

		layout.set_buffer(mapped, layout.get_required_size());

		for (unsigned level = 0; level < copy_levels; level++)
		{
			const auto& mip_info = layout.get_mip_info(level);
			uint32_t dst_height_stride = layout.get_layer_size(level);
			size_t row_size = layout.get_row_size(level);

			for (unsigned layer = 0; layer < info.layers; layer++, index++)
			{
				uint32_t src_row_length =
					initial[index].row_length ? initial[index].row_length : mip_info.row_length;
				uint32_t src_array_height =
					initial[index].image_height ? initial[index].image_height : mip_info.image_height;

				uint32_t src_row_stride = layout.row_byte_stride(src_row_length);
				uint32_t src_height_stride = layout.layer_byte_stride(src_array_height, src_row_stride);

				uint8_t* dst = static_cast<uint8_t*>(layout.data(layer, level));
				const uint8_t* src = static_cast<const uint8_t*>(initial[index].data);

				for (uint32_t z = 0; z < mip_info.depth; z++)
					for (uint32_t y = 0; y < mip_info.block_image_height; y++)
						memcpy(dst + z * dst_height_stride + y * row_size, src + z * src_height_stride + y * src_row_stride, row_size);
			}
		}

		UnmapHostBuffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT);
		layout.build_buffer_image_copies(result.blits);
		return result;
	}

	ImageHandle Device::CreateImage(const ImageCreateInfo& create_info, const ImageInitialData* initial)
	{
		if (initial)
		{
			auto staging_buffer = CreateImageStagingBuffer(create_info, initial);
			return CreateImageFromStagingBuffer(create_info, &staging_buffer);
		}
		else
			return CreateImageFromStagingBuffer(create_info, nullptr);
	}

	ImageHandle Device::CreateImageFromStagingBuffer(const ImageCreateInfo& create_info, const InitialImageBuffer* staging_buffer)
	{
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

		uint32_t queue_flags = create_info.misc & (IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT | IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT | IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT | IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT);
		bool concurrent_queue = queue_flags != 0;
		if (concurrent_queue)
		{
			info.sharingMode = VK_SHARING_MODE_CONCURRENT;

			const auto add_unique_family = [&](uint32_t family) {
				for (uint32_t i = 0; i < info.queueFamilyIndexCount; i++)
				{
					if (sharing_indices[i] == family)
						return;
				}
				sharing_indices[info.queueFamilyIndexCount++] = family;
			};

			if (queue_flags & (IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT | IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT))
				add_unique_family(graphics_queue_family_index);
			if (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT)
				add_unique_family(compute_queue_family_index);
			if (staging_buffer || (queue_flags & IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT) != 0)
				add_unique_family(transfer_queue_family_index);

			if (info.queueFamilyIndexCount > 1)
				info.pQueueFamilyIndices = sharing_indices;
			else
			{
				info.pQueueFamilyIndices = nullptr;
				info.queueFamilyIndexCount = 0;
				info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
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
			VK_ASSERT(create_info.usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);

			alloc_info.usage = VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		}
		else if (create_info.domain == ImageDomain::LinearHost)
		{
			alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			alloc_info.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		}
		else if (create_info.domain == ImageDomain::LinearHostCached)
		{
			alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			alloc_info.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
			alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
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
				handle->GetView().SetSRBGView(holder.srgb_view);
			}

			// Set possible dstStage and dstAccess.
			handle->SetStageFlags(ImageUsageToPossibleStages(info.usage));
			handle->SetAccessFlags(image_usage_to_possible_access(info.usage));
		}

		// Copy initial data to texture.
		if (staging_buffer)
		{
			VK_ASSERT(create_info.domain != ImageDomain::Transient);
			VK_ASSERT(create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED);
			bool generate_mips = (create_info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;

			// If graphics_queue != transfer_queue, we will use a semaphore, so no srcAccess mask is necessary.
			VkAccessFlags final_transition_src_access = 0;
			if (generate_mips)
				final_transition_src_access = VK_ACCESS_TRANSFER_READ_BIT; // Validation complains otherwise.
			else if (graphics_queue == transfer_queue)
				final_transition_src_access = VK_ACCESS_TRANSFER_WRITE_BIT;

			VkAccessFlags prepare_src_access = graphics_queue == transfer_queue ? VK_ACCESS_TRANSFER_WRITE_BIT : 0;
			bool need_mipmap_barrier = true;
			bool need_initial_barrier = true;

			// Now we've used the TRANSFER queue to copy data over to the GPU.
			// For mipmapping, we're now moving over to graphics,
			// the transfer queue is designed for CPU <-> GPU and that's it.

			// For concurrent queue mode, we just need to inject a semaphore.
			// For non-concurrent queue mode, we will have to inject ownership transfer barrier if the queue families do not match.

			auto graphics_cmd = RequestCommandBuffer(CommandBuffer::Type::Generic);
			CommandBufferHandle transfer_cmd;

			// Don't split the upload into multiple command buffers unless we have to.
			if (transfer_queue != graphics_queue)
				transfer_cmd = RequestCommandBuffer(CommandBuffer::Type::AsyncTransfer);
			else
				transfer_cmd = graphics_cmd;

			transfer_cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT);

			transfer_cmd->CopyBufferToImage(*handle, *staging_buffer->buffer, staging_buffer->blits.size(), staging_buffer->blits.data());

			if (transfer_queue != graphics_queue)
			{
				VkPipelineStageFlags dst_stages =
					generate_mips ? VkPipelineStageFlags(VK_PIPELINE_STAGE_TRANSFER_BIT) : handle->GetStageFlags();

				// We can't just use semaphores, we will also need a release + acquire barrier to marshal ownership from
				// transfer queue over to graphics ...
				if (!concurrent_queue && transfer_queue_family_index != graphics_queue_family_index)
				{
					need_mipmap_barrier = false;

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
						need_initial_barrier = false;
					}

					release.subresourceRange.aspectMask = format_to_aspect_mask(info.format);
					release.subresourceRange.layerCount = info.arrayLayers;

					VkImageMemoryBarrier acquire = release;
					acquire.srcAccessMask = 0;

					if (generate_mips)
						acquire.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					else
						acquire.dstAccessMask = handle->GetAccessFlags() & ImageLayoutToPossibleAccess(create_info.initial_layout);

					transfer_cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
						VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
						0, nullptr, 0, nullptr, 1, &release);

					graphics_cmd->Barrier(dst_stages,
						dst_stages,
						0, nullptr, 0, nullptr, 1, &acquire);
				}

				Semaphore sem;
				Submit(transfer_cmd, nullptr, 1, &sem);
				AddWaitSemaphore(CommandBuffer::Type::Generic, sem, dst_stages, true);
			}

			if (generate_mips)
			{
				graphics_cmd->BarrierPrepareGenerateMipmap(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, prepare_src_access, need_mipmap_barrier);
				graphics_cmd->GenerateMipmap(*handle);
			}

			if (need_initial_barrier)
			{
				graphics_cmd->ImageBarrier(
					*handle, generate_mips ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					create_info.initial_layout,
					VK_PIPELINE_STAGE_TRANSFER_BIT, final_transition_src_access,
					handle->GetStageFlags(),
					handle->GetAccessFlags() & ImageLayoutToPossibleAccess(create_info.initial_layout));
			}

			bool share_compute = concurrent_queue && graphics_queue != compute_queue;
			bool share_async_graphics = GetPhysicalQueueType(CommandBuffer::Type::AsyncGraphics) == CommandBuffer::Type::AsyncCompute;

			// For concurrent queue, make sure that compute can see the final image as well.
			// Also add semaphore if the compute queue can be used for async graphics as well.
			if (share_compute || share_async_graphics)
			{
				Semaphore sem;
				Submit(graphics_cmd, nullptr, 1, &sem);

				VkPipelineStageFlags dst_stages = handle->GetStageFlags();
				if (graphics_queue_family_index != compute_queue_family_index)
					dst_stages &= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
				AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem, dst_stages, true);
			}
			else
				Submit(graphics_cmd);
		}
		else if (create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
		{
			VK_ASSERT(create_info.domain != ImageDomain::Transient);
			auto cmd = RequestCommandBuffer(CommandBuffer::Type::Generic);
			cmd->ImageBarrier(*handle, info.initialLayout, create_info.initial_layout,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, handle->GetStageFlags(),
				handle->GetAccessFlags() &
				ImageLayoutToPossibleAccess(create_info.initial_layout));

			// For concurrent queue, make sure that compute can see the final image as well.
			if (concurrent_queue && graphics_queue != compute_queue)
			{
				Semaphore sem;
				Submit(cmd, nullptr, 1, &sem);
				AddWaitSemaphore(CommandBuffer::Type::AsyncCompute,
					sem, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
			}
			else
				Submit(cmd);
		}

		return handle;
	}
	
	/////////////////////////////
	//Bindless descriptors///////
	////////////////////////////

	BindlessDescriptorPoolHandle Device::CreateBindlessDescriptorPool(BindlessResourceType type,
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
			pool = allocator->allocate_bindless_pool(num_sets, num_descriptors);

		if (!pool)
		{
			QM_LOG_ERROR("Failed to allocate bindless pool.\n");
			return BindlessDescriptorPoolHandle{ nullptr };
		}

		auto* handle = handle_pool.bindless_descriptor_pool.allocate(this, allocator, pool);
		return BindlessDescriptorPoolHandle{ handle };
	}

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