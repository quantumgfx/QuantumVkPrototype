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
		add_wait_semaphore_nolock(type, semaphore, stages, flush);
	}

	void Device::add_wait_semaphore_nolock(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush)
	{
		VK_ASSERT(stages != 0);
		if (flush)
			flush_frame(type);
		auto& data = get_queue_data(type);

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

	PipelineLayout* Device::request_pipeline_layout(const CombinedResourceLayout& layout)
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

	DescriptorSetAllocator* Device::request_descriptor_set_allocator(const DescriptorSetLayout& layout, const uint32_t* stages_for_bindings)
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

	void Device::bake_program(Program& program)
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
		program.SetPipelineLayout(request_pipeline_layout(layout));
	}

	bool Device::InitPipelineCache(const uint8_t* data, size_t size)
	{
		static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);

		VkPipelineCacheCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
		if (!data || size < uuid_size)
		{
			QM_LOG_INFO("Creating a fresh pipeline cache.\n");
		}
		else if (memcmp(data, gpu_props.pipelineCacheUUID, uuid_size) != 0)
		{
			QM_LOG_INFO("Pipeline cache UUID changed.\n");
		}
		else
		{
			info.initialDataSize = size - uuid_size;
			info.pInitialData = data + uuid_size;
			QM_LOG_INFO("Initializing pipeline cache.\n");
		}

		if (pipeline_cache != VK_NULL_HANDLE)
			table->vkDestroyPipelineCache(device, pipeline_cache, nullptr);
		pipeline_cache = VK_NULL_HANDLE;
		return table->vkCreatePipelineCache(device, &info, nullptr, &pipeline_cache) == VK_SUCCESS;
	}

	static inline char to_hex(uint8_t v)
	{
		if (v < 10)
			return char('0' + v);
		else
			return char('a' + (v - 10));
	}

	string Device::get_pipeline_cache_string() const
	{
		string res;
		res.reserve(sizeof(gpu_props.pipelineCacheUUID) * 2);

		for (auto& c : gpu_props.pipelineCacheUUID)
		{
			res += to_hex(uint8_t((c >> 4) & 0xf));
			res += to_hex(uint8_t(c & 0xf));
		}

		return res;
	}

	size_t Device::GetPipelineCacheSize()
	{
		if (pipeline_cache == VK_NULL_HANDLE)
			return 0;

		static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
		size_t size = 0;
		if (table->vkGetPipelineCacheData(device, pipeline_cache, &size, nullptr) != VK_SUCCESS)
		{
			QM_LOG_ERROR("Failed to get pipeline cache data.\n");
			return 0;
		}

		return size + uuid_size;
	}

	bool Device::GetPipelineCacheData(uint8_t* data, size_t size)
	{
		if (pipeline_cache == VK_NULL_HANDLE)
			return false;

		static const auto uuid_size = sizeof(gpu_props.pipelineCacheUUID);
		if (size < uuid_size)
			return false;

		size -= uuid_size;
		memcpy(data, gpu_props.pipelineCacheUUID, uuid_size);
		data += uuid_size;

		if (table->vkGetPipelineCacheData(device, pipeline_cache, &size, data) != VK_SUCCESS)
		{
			QM_LOG_ERROR("Failed to get pipeline cache data.\n");
			return false;
		}

		return true;
	}

	void Device::init_workarounds()
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

	void Device::SetContext(const ContextHandle& context_)
	{
		context = context_;
		table = &context_->GetDeviceTable();

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
		//ext = context_->GetEnabledDeviceFeatures();

		init_workarounds();

		init_stock_samplers();
		init_pipeline_cache();

		init_timeline_semaphores();
		init_bindless();

#ifdef ANDROID
		InitFrameContexts(3); // Android needs a bit more ... ;)
#else
		InitFrameContexts(2); // By default, regular double buffer between CPU and GPU.
#endif

		managers.memory.Init(this);
		managers.semaphore.init(this);
		managers.fence.init(this);
		managers.event.init(this);
		managers.vbo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			ImplementationQuirks::get().staging_need_device_local);
		managers.ibo.init(this, 4 * 1024, 16, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			ImplementationQuirks::get().staging_need_device_local);
		managers.ubo.init(this, 256 * 1024, std::max<VkDeviceSize>(16u, gpu_props.limits.minUniformBufferOffsetAlignment),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			ImplementationQuirks::get().staging_need_device_local);
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

#ifdef QM_VULKAN_FILESYSTEM
		init_shader_manager_cache();
#endif

		init_calibrated_timestamps();
	}

	void Device::SetContext(const ContextHandle& context_, void* pipeline_state_data, size_t pipeline_state_size)
	{
		context = context_;
		table = &context_->GetDeviceTable();

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
		ext = &context_->GetEnabledDeviceFeatures();

		init_workarounds();

		init_stock_samplers();
		init_pipeline_cache();

		init_timeline_semaphores();
		init_bindless();

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

#ifdef QM_VULKAN_FOSSILIZE
		init_pipeline_state(pipeline_state_data, pipeline_state_size);
#endif
#ifdef QM_VULKAN_FILESYSTEM
		init_shader_manager_cache();
#endif

		init_calibrated_timestamps();
	}

	void Device::init_bindless()
	{
		if (!ext->supports_descriptor_indexing)
			return;

		DescriptorSetLayout layout;

		layout.array_size[0] = DescriptorSetLayout::UNSIZED_ARRAY;
		for (unsigned i = 1; i < VULKAN_NUM_BINDINGS; i++)
			layout.array_size[i] = 1;

		layout.separate_image_mask = 1;
		uint32_t stages_for_sets[VULKAN_NUM_BINDINGS] = { VK_SHADER_STAGE_ALL };
		bindless_sampled_image_allocator_integer = request_descriptor_set_allocator(layout, stages_for_sets);
		layout.fp_mask = 1;
		bindless_sampled_image_allocator_fp = request_descriptor_set_allocator(layout, stages_for_sets);
	}

	void Device::init_timeline_semaphores()
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

	void Device::init_stock_samplers()
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

			samplers[i] = create_sampler(info, mode);
		}
	}

	static void request_block(Device& device, BufferBlock& block, VkDeviceSize size, BufferPool& pool, std::vector<BufferBlock>* dma, std::vector<BufferBlock>& recycle)
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

	void Device::request_vertex_block(BufferBlock& block, VkDeviceSize size)
	{
		LOCK();
		request_vertex_block_nolock(block, size);
	}

	void Device::request_vertex_block_nolock(BufferBlock& block, VkDeviceSize size)
	{
		request_block(*this, block, size, managers.vbo, &dma.vbo, frame().vbo_blocks);
	}

	void Device::request_index_block(BufferBlock& block, VkDeviceSize size)
	{
		LOCK();
		request_index_block_nolock(block, size);
	}

	void Device::request_index_block_nolock(BufferBlock& block, VkDeviceSize size)
	{
		request_block(*this, block, size, managers.ibo, &dma.ibo, frame().ibo_blocks);
	}

	void Device::request_uniform_block(BufferBlock& block, VkDeviceSize size)
	{
		LOCK();
		request_uniform_block_nolock(block, size);
	}

	void Device::request_uniform_block_nolock(BufferBlock& block, VkDeviceSize size)
	{
		request_block(*this, block, size, managers.ubo, &dma.ubo, frame().ubo_blocks);
	}

	void Device::request_staging_block(BufferBlock& block, VkDeviceSize size)
	{
		LOCK();
		request_staging_block_nolock(block, size);
	}

	void Device::request_staging_block_nolock(BufferBlock& block, VkDeviceSize size)
	{
		request_block(*this, block, size, managers.staging, nullptr, frame().staging_blocks);
	}

	void Device::Submit(CommandBufferHandle& cmd, Fence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		cmd->EndDebugChannel();

		LOCK();
		submit_nolock(move(cmd), fence, semaphore_count, semaphores);
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

	void Device::submit_nolock(CommandBufferHandle cmd, Fence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		auto type = cmd->GetCommandBufferType();
		auto& submissions = get_queue_submissions(type);
#ifdef VULKAN_DEBUG
		auto& pool = get_command_pool(type, cmd->GetThreadIndex());
		pool.SignalSubmitted(cmd->GetCommandBuffer());
#endif

		bool profiled_submit = cmd->HasProfiling();

		if (profiled_submit)
		{
			QM_LOG_INFO("Submitting profiled command buffer, draining GPU.\n");
			auto& query_pool = get_performance_query_pool(type);
			// Profiled submit, drain GPU before submitting to make sure there's no overlap going on.
			query_pool.end_command_buffer(cmd->GetCommandBuffer());
			Fence drain_fence;
			submit_empty_nolock(type, &drain_fence, 0, nullptr, -1);
			drain_fence->wait();
			drain_fence->set_internal_sync_object();
		}

		cmd->End();
		submissions.push_back(move(cmd));

		InternalFence signalled_fence;

		if (fence || semaphore_count)
		{
			submit_queue(type, fence ? &signalled_fence : nullptr,
				semaphore_count, semaphores,
				profiled_submit ? 0 : -1);
		}

		if (fence)
		{
			VK_ASSERT(!*fence);
			if (signalled_fence.value)
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.value, signalled_fence.timeline));
			else
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.fence));
		}

		if (profiled_submit)
		{
			// Drain queue again and report results.
			QM_LOG_INFO("Submitted profiled command buffer, draining GPU and report ...\n");
			auto& query_pool = get_performance_query_pool(type);
			Fence drain_fence;
			submit_empty_nolock(type, &drain_fence, 0, nullptr, fence || semaphore_count ? -1 : 0);
			drain_fence->wait();
			drain_fence->set_internal_sync_object();
			query_pool.report();
		}

		decrement_frame_counter_nolock();
	}

	void Device::SubmitEmpty(CommandBuffer::Type type, Fence* fence,
		unsigned semaphore_count, Semaphore* semaphores)
	{
		LOCK();
		submit_empty_nolock(type, fence, semaphore_count, semaphores, -1);
	}

	void Device::submit_empty_nolock(CommandBuffer::Type type, Fence* fence,
		unsigned semaphore_count, Semaphore* semaphores, int profiling_iteration)
	{
		if (type != CommandBuffer::Type::AsyncTransfer)
			flush_frame(CommandBuffer::Type::AsyncTransfer);

		InternalFence signalled_fence;
		submit_queue(type, fence ? &signalled_fence : nullptr, semaphore_count, semaphores, profiling_iteration);
		if (fence)
		{
			if (signalled_fence.value)
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.value, signalled_fence.timeline));
			else
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.fence));
		}
	}

	void Device::submit_empty_inner(CommandBuffer::Type type, InternalFence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		auto& data = get_queue_data(type);
		VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		VkTimelineSemaphoreSubmitInfoKHR timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

		if (ext->timeline_semaphore_features.timelineSemaphore)
			submit.pNext = &timeline_info;

		VkSemaphore timeline_semaphore = data.timeline_semaphore;
		uint64_t timeline_value = ++data.current_timeline;

		VkQueue queue = get_vk_queue(type);
		switch (type)
		{
		default:
		case CommandBuffer::Type::Generic:
			frame().timeline_fence_graphics = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			if (ext.timeline_semaphore_features.timelineSemaphore)
			{
				LOGI("Signal graphics: (%p) %u\n",
					reinterpret_cast<void*>(timeline_semaphore),
					unsigned(data.current_timeline));
			}
#endif
			break;

		case CommandBuffer::Type::AsyncCompute:
			frame().timeline_fence_compute = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			if (ext.timeline_semaphore_features.timelineSemaphore)
			{
				LOGI("Signal compute: (%p) %u\n",
					reinterpret_cast<void*>(timeline_semaphore),
					unsigned(data.current_timeline));
			}
#endif
			break;

		case CommandBuffer::Type::AsyncTransfer:
			frame().timeline_fence_transfer = data.current_timeline;
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
					frame().recycled_semaphores.push_back(wait);
				else
					frame().destroyed_semaphores.push_back(wait);
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

		QueryPoolHandle start_ts, end_ts;
		if (json_timestamp_origin)
			start_ts = write_calibrated_timestamp_nolock();

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

		if (json_timestamp_origin)
		{
			end_ts = write_calibrated_timestamp_nolock();
			register_time_interval_nolock("CPU", std::move(start_ts), std::move(end_ts), "submit", "");
		}

		if (result != VK_SUCCESS)
			QM_LOG_ERROR("vkQueueSubmit failed (code: %d).\n", int(result));
		if (result == VK_ERROR_DEVICE_LOST)
			report_checkpoints();

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

		LOGI("Empty submission to %s queue:\n", queue_name);
		for (uint32_t i = 0; i < submit.waitSemaphoreCount; i++)
		{
			LOGI("  Waiting for semaphore: %llx in stages %s\n",
				reinterpret_cast<unsigned long long>(submit.pWaitSemaphores[i]),
				stage_flags_to_string(submit.pWaitDstStageMask[i]).c_str());
		}

		for (uint32_t i = 0; i < submit.signalSemaphoreCount; i++)
		{
			LOGI("  Signalling semaphore: %llx\n",
				reinterpret_cast<unsigned long long>(submit.pSignalSemaphores[i]));
		}
#endif
	}

	Fence Device::request_legacy_fence()
	{
		VkFence fence = managers.fence.request_cleared_fence();
		return Fence(handle_pool.fences.allocate(this, fence));
	}

	void Device::submit_staging(CommandBufferHandle& cmd, VkBufferUsageFlags usage, bool flush)
	{
		auto access = BufferUsageToPossibleAccess(usage);
		auto stages = BufferUsageToPossibleStages(usage);
		VkQueue src_queue = get_vk_queue(cmd->GetCommandBufferType());

		if (src_queue == graphics_queue && src_queue == compute_queue)
		{
			// For single-queue systems, just use a pipeline barrier.
			cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, stages, access);
			submit_nolock(cmd, nullptr, 0, nullptr);
		}
		else
		{
			auto compute_stages = stages &
				(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
					VK_PIPELINE_STAGE_TRANSFER_BIT |
					VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);

			auto compute_access = access &
				(VK_ACCESS_SHADER_READ_BIT |
					VK_ACCESS_SHADER_WRITE_BIT |
					VK_ACCESS_TRANSFER_READ_BIT |
					VK_ACCESS_UNIFORM_READ_BIT |
					VK_ACCESS_TRANSFER_WRITE_BIT |
					VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

			auto graphics_stages = stages;

			if (src_queue == graphics_queue)
			{
				cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, graphics_stages, access);

				if (compute_stages != 0)
				{
					Semaphore sem;
					submit_nolock(cmd, nullptr, 1, &sem);
					add_wait_semaphore_nolock(CommandBuffer::Type::AsyncCompute, sem, compute_stages, flush);
				}
				else
					submit_nolock(cmd, nullptr, 0, nullptr);
			}
			else if (src_queue == compute_queue)
			{
				cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, compute_stages, compute_access);

				if (graphics_stages != 0)
				{
					Semaphore sem;
					submit_nolock(cmd, nullptr, 1, &sem);
					add_wait_semaphore_nolock(CommandBuffer::Type::Generic, sem, graphics_stages, flush);
				}
				else
					submit_nolock(cmd, nullptr, 0, nullptr);
			}
			else
			{
				if (graphics_stages != 0 && compute_stages != 0)
				{
					Semaphore semaphores[2];
					submit_nolock(cmd, nullptr, 2, semaphores);
					add_wait_semaphore_nolock(CommandBuffer::Type::Generic, semaphores[0], graphics_stages, flush);
					add_wait_semaphore_nolock(CommandBuffer::Type::AsyncCompute, semaphores[1], compute_stages, flush);
				}
				else if (graphics_stages != 0)
				{
					Semaphore sem;
					submit_nolock(cmd, nullptr, 1, &sem);
					add_wait_semaphore_nolock(CommandBuffer::Type::Generic, sem, graphics_stages, flush);
				}
				else if (compute_stages != 0)
				{
					Semaphore sem;
					submit_nolock(cmd, nullptr, 1, &sem);
					add_wait_semaphore_nolock(CommandBuffer::Type::AsyncCompute, sem, compute_stages, flush);
				}
				else
					submit_nolock(cmd, nullptr, 0, nullptr);
			}
		}
	}

	void Device::submit_queue(CommandBuffer::Type type, InternalFence* fence, unsigned semaphore_count, Semaphore* semaphores, int profiling_iteration)
	{
		type = GetPhysicalQueueType(type);

		// Always check if we need to flush pending transfers.
		if (type != CommandBuffer::Type::AsyncTransfer)
			flush_frame(CommandBuffer::Type::AsyncTransfer);

		auto& data = get_queue_data(type);
		auto& submissions = get_queue_submissions(type);

		if (submissions.empty())
		{
			if (fence || semaphore_count)
				submit_empty_inner(type, fence, semaphore_count, semaphores);
			return;
		}

		VkSemaphore timeline_semaphore = data.timeline_semaphore;
		uint64_t timeline_value = ++data.current_timeline;

		VkQueue queue = get_vk_queue(type);
		switch (type)
		{
		default:
		case CommandBuffer::Type::Generic:
			frame().timeline_fence_graphics = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Signal graphics: (%p) %u\n",
				reinterpret_cast<void*>(timeline_semaphore),
				unsigned(data.current_timeline));
#endif
			break;

		case CommandBuffer::Type::AsyncCompute:
			frame().timeline_fence_compute = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Signal compute: (%p) %u\n",
				reinterpret_cast<void*>(timeline_semaphore),
				unsigned(data.current_timeline));
#endif
			break;

		case CommandBuffer::Type::AsyncTransfer:
			frame().timeline_fence_transfer = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Signal transfer: (%p) %u\n",
				reinterpret_cast<void*>(timeline_semaphore),
				unsigned(data.current_timeline));
#endif
			break;
		}

		SmallVector<VkCommandBuffer> cmds;
		cmds.reserve(submissions.size());

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
		wait_stages[0] = move(data.wait_stages);

		for (auto& semaphore : data.wait_semaphores)
		{
			auto wait = semaphore->consume();
			if (!semaphore->get_timeline_value())
			{
				if (semaphore->can_recycle())
					frame().recycled_semaphores.push_back(wait);
				else
					frame().destroyed_semaphores.push_back(wait);
			}
			wait_counts[0].push_back(semaphore->get_timeline_value());
			waits[0].push_back(wait);
		}
		data.wait_stages.clear();
		data.wait_semaphores.clear();

		for (auto& cmd : submissions)
		{
			if (cmd->SwapchainTouched() && !wsi.touched && !wsi.consumed)
			{
				if (!cmds.empty())
				{
					// Push all pending cmd buffers to their own submission.
					submits.emplace_back();

					timeline_infos.emplace_back();
					auto& timeline_info = timeline_infos.back();
					timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

					auto& submit = submits.back();
					submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
					if (ext->timeline_semaphore_features.timelineSemaphore)
						submit.pNext = &timeline_info;

					submit.commandBufferCount = cmds.size() - last_cmd;
					submit.pCommandBuffers = cmds.data() + last_cmd;
					last_cmd = cmds.size();
				}
				wsi.touched = true;
			}

			cmds.push_back(cmd->GetCommandBuffer());
		}

		if (cmds.size() > last_cmd)
		{
			unsigned index = submits.size();

			// Push all pending cmd buffers to their own submission.
			timeline_infos.emplace_back();
			auto& timeline_info = timeline_infos.back();
			timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

			submits.emplace_back();
			auto& submit = submits.back();
			submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

			if (ext->timeline_semaphore_features.timelineSemaphore)
				submit.pNext = &timeline_info;

			submit.commandBufferCount = cmds.size() - last_cmd;
			submit.pCommandBuffers = cmds.data() + last_cmd;
			if (wsi.touched && !wsi.consumed)
			{
				static const VkFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				if (wsi.acquire && wsi.acquire->get_semaphore() != VK_NULL_HANDLE)
				{
					VK_ASSERT(wsi.acquire->is_signalled());
					VkSemaphore sem = wsi.acquire->consume();

					waits[index].push_back(sem);
					wait_counts[index].push_back(wsi.acquire->get_timeline_value());
					wait_stages[index].push_back(wait);

					if (!wsi.acquire->get_timeline_value())
					{
						if (wsi.acquire->can_recycle())
							frame().recycled_semaphores.push_back(sem);
						else
							frame().destroyed_semaphores.push_back(sem);
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

		VkFence cleared_fence = fence && !ext->timeline_semaphore_features.timelineSemaphore ?
			managers.fence.request_cleared_fence() :
			VK_NULL_HANDLE;

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

		VkPerformanceQuerySubmitInfoKHR profiling_infos[2];

		for (unsigned i = 0; i < submits.size(); i++)
		{
			auto& submit = submits[i];
			auto& timeline_submit = timeline_infos[i];

			if (profiling_iteration >= 0)
			{
				profiling_infos[i] = { VK_STRUCTURE_TYPE_PERFORMANCE_QUERY_SUBMIT_INFO_KHR };
				profiling_infos[i].counterPassIndex = uint32_t(profiling_iteration);
				if (submit.pNext)
					timeline_submit.pNext = &profiling_infos[i];
				else
					submit.pNext = &profiling_infos[i];
			}

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

		QueryPoolHandle start_ts, end_ts;
		if (json_timestamp_origin)
			start_ts = write_calibrated_timestamp_nolock();

		if (queue_lock_callback)
			queue_lock_callback();
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		if (cleared_fence)
			QM_LOG_ERROR("Signalling fence: %llx\n", reinterpret_cast<unsigned long long>(cleared_fence));
#endif
		VkResult result = table->vkQueueSubmit(queue, submits.size(), submits.data(), cleared_fence);
		if (ImplementationQuirks::get().queue_wait_on_submission)
			table->vkQueueWaitIdle(queue);
		if (queue_unlock_callback)
			queue_unlock_callback();

		if (json_timestamp_origin)
		{
			end_ts = write_calibrated_timestamp_nolock();
			register_time_interval_nolock("CPU", std::move(start_ts), std::move(end_ts), "submit", "");
		}

		if (result != VK_SUCCESS)
			QM_LOG_ERROR("vkQueueSubmit failed (code: %d).\n", int(result));
		if (result == VK_ERROR_DEVICE_LOST)
			report_checkpoints();
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

	void Device::flush_frame(CommandBuffer::Type type)
	{
		if (type == CommandBuffer::Type::AsyncTransfer)
			sync_buffer_blocks();
		submit_queue(type, nullptr, 0, nullptr);
	}

	void Device::sync_buffer_blocks()
	{
		if (dma.vbo.empty() && dma.ibo.empty() && dma.ubo.empty())
			return;

		VkBufferUsageFlags usage = 0;

		auto cmd = request_command_buffer_nolock(get_thread_index(), CommandBuffer::Type::AsyncTransfer, false);

		cmd->BeginRegion("buffer-block-sync");

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

		cmd->EndRegion();

		// Do not flush graphics or compute in this context.
		// We must be able to inject semaphores into all currently enqueued graphics / compute.
		submit_staging(cmd, usage, false);
	}

	void Device::EndFrameContext()
	{
		DRAIN_FRAME_LOCK();
		end_frame_nolock();
	}

}