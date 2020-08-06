#pragma once

#include "vulkan_headers.hpp"
#include "vulkan_common.hpp"

#include "buffer.hpp"
#include "command_buffer.hpp"
#include "command_pool.hpp"
#include "fence.hpp"
#include "managers/fence_manager.hpp"
#include "image.hpp"
#include "memory_allocator.hpp"
#include "render_pass.hpp"
#include "sampler.hpp"
#include "semaphore.hpp"
#include "managers/semaphore_manager.hpp"
#include "managers/event_manager.hpp"
#include "shader.hpp"
#include "context.hpp"
#include "query_pool.hpp"
#include "buffer_pool.hpp"
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <stdio.h>

#ifdef QM_VULKAN_FILESYSTEM
#include "shader_manager.hpp"
#include "texture_manager.hpp"
#endif

#ifdef QM_VULKAN_MT
#include <atomic>
#include <mutex>
#include <condition_variable>
#endif

#ifdef QM_VULKAN_FOSSILIZE
#include "fossilize.hpp"
#include "threading/thread_group.hpp"
#endif

#include "quirks.hpp"
#include "utils/small_vector.hpp"

namespace Vulkan
{
	enum class SwapchainRenderPass
	{
		ColorOnly,
		Depth,
		DepthStencil
	};

	struct InitialImageBuffer
	{
		BufferHandle buffer;
		std::vector<VkBufferImageCopy> blits;
	};

	struct HandlePool
	{
		VulkanObjectPool<Buffer> buffers;
		VulkanObjectPool<Image> images;
		VulkanObjectPool<LinearHostImage> linear_images;
		VulkanObjectPool<ImageView> image_views;
		VulkanObjectPool<BufferView> buffer_views;
		VulkanObjectPool<Sampler> samplers;
		VulkanObjectPool<FenceHolder> fences;
		VulkanObjectPool<SemaphoreHolder> semaphores;
		VulkanObjectPool<EventHolder> events;
		VulkanObjectPool<QueryPoolResult> query;
		VulkanObjectPool<CommandBuffer> command_buffers;
		VulkanObjectPool<BindlessDescriptorPool> bindless_descriptor_pool;
	};

	class DebugChannelInterface
	{
	public:
		union Word
		{
			uint32_t u32;
			int32_t s32;
			float f32;
		};
		virtual void message(const std::string& tag, uint32_t x, uint32_t y, uint32_t z,
			uint32_t code, uint32_t word_count, const Word* words) = 0;
	};

	class Device
#ifdef QM_VULKAN_FOSSILIZE
		: public Fossilize::StateCreatorInterface
#endif
	{
	public:
		// Device-based objects which need to poke at internal data structures when their lifetimes end.
		// Don't want to expose a lot of internal guts to make this work.
		friend class QueryPool;
		friend struct QueryPoolResultDeleter;
		friend class EventHolder;
		friend struct EventHolderDeleter;
		friend class SemaphoreHolder;
		friend struct SemaphoreHolderDeleter;
		friend class FenceHolder;
		friend struct FenceHolderDeleter;
		friend class Sampler;
		friend struct SamplerDeleter;
		friend class Buffer;
		friend struct BufferDeleter;
		friend class BufferView;
		friend struct BufferViewDeleter;
		friend class ImageView;
		friend struct ImageViewDeleter;
		friend class Image;
		friend struct ImageDeleter;
		friend struct LinearHostImageDeleter;
		friend class CommandBuffer;
		friend struct CommandBufferDeleter;
		friend class BindlessDescriptorPool;
		friend struct BindlessDescriptorPoolDeleter;
		friend class Program;
		friend class WSI;
		friend class Cookie;
		friend class Framebuffer;
		friend class PipelineLayout;
		friend class FramebufferAllocator;
		friend class RenderPass;
		friend class Texture;
		friend class DescriptorSetAllocator;
		friend class Shader;
		friend class ImageResourceHolder;

		Device();
		~Device();

		// No move-copy.
		void operator=(Device&&) = delete;
		Device(Device&&) = delete;

		// Only called by main thread, during setup phase.
		void SetContext(const Context& context);
		void InitSwapchain(const std::vector<VkImage>& swapchain_images, unsigned width, unsigned height, VkFormat format);
		void InitExternalSwapchain(const std::vector<ImageHandle>& swapchain_images);
		void InitFrameContexts(unsigned count);
		const VolkDeviceTable& GetDeviceTable() const;

		// Profiling
		bool InitPerformanceCounters(const std::vector<std::string>& names);
		bool AcquireProfiling();
		void ReleaseProfiling();
		void QueryAvailablePerformanceCounters(CommandBuffer::Type type, uint32_t* count, const VkPerformanceCounterKHR** counters, const VkPerformanceCounterDescriptionKHR** desc);
		bool InitTimestampTrace(const char* path);

		ImageView& GetSwapchainView();
		ImageView& GetSwapchainView(unsigned index);
		unsigned GetNumSwapchainImages() const;
		unsigned GetNumFrameContexts() const;
		unsigned GetSwapchainIndex() const;
		unsigned GetCurrentFrameContext() const;

		size_t GetPipelineCacheSize();
		bool GetPipelineCacheData(uint8_t* data, size_t size);
		bool InitPipelineCache(const uint8_t* data, size_t size);

		// Frame-pushing interface.
		void NextFrameContext();
		void WaitIdle();
		void EndFrameContext();

		// Set names for objects for debuggers and profilers.
		void SetName(const Buffer& buffer, const char* name);
		void SetName(const Image& image, const char* name);
		void SetName(const CommandBuffer& cmd, const char* name);

		// Submission interface, may be called from any thread at any time.
		void FlushFrame();
		CommandBufferHandle RequestCommandBuffer(CommandBuffer::Type type = CommandBuffer::Type::Generic);
		CommandBufferHandle RequestCommandBufferForThread(unsigned thread_index, CommandBuffer::Type type = CommandBuffer::Type::Generic);

		CommandBufferHandle RequestProfiledCommandBuffer(CommandBuffer::Type type = CommandBuffer::Type::Generic);
		CommandBufferHandle RequestProfiledCommandBufferForThread(unsigned thread_index, CommandBuffer::Type type = CommandBuffer::Type::Generic);

		void Submit(CommandBufferHandle& cmd, Fence* fence = nullptr, unsigned semaphore_count = 0, Semaphore* semaphore = nullptr);
		void SubmitEmpty(CommandBuffer::Type type,  Fence* fence = nullptr,unsigned semaphore_count = 0, Semaphore* semaphore = nullptr);
		void AddWaitSemaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush);
		CommandBuffer::Type GetPhysicalQueueType(CommandBuffer::Type queue_type) const;
		void RegisterTimeInterval(std::string tid, QueryPoolHandle start_ts, QueryPoolHandle end_ts, std::string tag, std::string extra = {});

		// Request shaders and programs. These objects are owned by the Device.
		Shader* RequestShader(const uint32_t* code, size_t size);
		Shader* RequestShaderByHash(Util::Hash hash);
		Program* RequestProgram(const uint32_t* vertex_data, size_t vertex_size, const uint32_t* fragment_data,
								 size_t fragment_size);
		Program* RequestProgram(const uint32_t* compute_data, size_t compute_size);
		Program* RequestProgram(Shader* vertex, Shader* fragment);
		Program* RequestProgram(Shader* compute);

		// Map and unmap buffer objects.
		void* MapHostBuffer(const Buffer& buffer, MemoryAccessFlags access);
		void UnmapHostBuffer(const Buffer& buffer, MemoryAccessFlags access);
		void* MapHostBuffer(const Buffer& buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length);
		void UnmapHostBuffer(const Buffer& buffer, MemoryAccessFlags access, VkDeviceSize offset, VkDeviceSize length);

		void* MapLinearHostImage(const LinearHostImage& image, MemoryAccessFlags access);
		void UnmapLinearHostImageAndSync(const LinearHostImage& image, MemoryAccessFlags access);

		// Create buffers and images.
		BufferHandle CreateBuffer(const BufferCreateInfo& info, const void* initial = nullptr);
		BufferHandle CreateImportedHostBuffer(const BufferCreateInfo& info, VkExternalMemoryHandleTypeFlagBits type, void* host_buffer);
		ImageHandle CreateImage(const ImageCreateInfo& info, const ImageInitialData* initial = nullptr);
		ImageHandle CreateImageFromStagingBuffer(const ImageCreateInfo& info, const InitialImageBuffer* buffer);
		LinearHostImageHandle CreateLinearHostImage(const LinearHostImageCreateInfo& info);

		// Create staging buffers for images.
		InitialImageBuffer CreateImageStagingBuffer(const ImageCreateInfo& info, const ImageInitialData* initial);
		InitialImageBuffer CreateImageStagingBuffer(const TextureFormatLayout& layout);

	/*#ifndef _WIN32
		ImageHandle create_imported_image(int fd,
										  VkDeviceSize size,
										  uint32_t memory_type,
										  VkExternalMemoryHandleTypeFlagBitsKHR handle_type,
										  const ImageCreateInfo& create_info);
	#endif*/

		// Create image view, buffer views and samplers.
		ImageViewHandle CreateImageView(const ImageViewCreateInfo& view_info);
		BufferViewHandle CreateBufferView(const BufferViewCreateInfo& view_info);
		SamplerHandle CreateSampler(const SamplerCreateInfo& info);

		BindlessDescriptorPoolHandle CreateBindlessDescriptorPool(BindlessResourceType type,  unsigned num_sets, unsigned num_descriptors);

		// Render pass helpers.
		bool ImageFormatIsSupported(VkFormat format, VkFormatFeatureFlags required, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL) const;
		void GetFormatProperties(VkFormat format, VkFormatProperties* properties);
		bool GetImageFormatProperties(VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
										 VkImageFormatProperties* properties);

		VkFormat GetDefaultDepthStencil_format() const;
		VkFormat GetDefaultDepthFormat() const;
		ImageView& GetTransientAttachment(unsigned width, unsigned height, VkFormat format,
											unsigned index = 0, unsigned samples = 1, unsigned layers = 1);
		RenderPassInfo GetSwapchainRenderPass(SwapchainRenderPass style);

		// Request legacy (non-timeline) semaphores.
		// Timeline semaphores are only used internally to reduce handle bloat.
		Semaphore RequestLegacySemaphore();
		Semaphore RequestExternalSemaphore(VkSemaphore semaphore, bool signalled);
	/*#ifndef _WIN32
		Semaphore request_imported_semaphore(int fd, VkExternalSemaphoreHandleTypeFlagBitsKHR handle_type);
	#endif*/

		VkDevice GetDevice() const
		{
			return device;
		}

		VkPhysicalDevice GetPhysicalDevice() const
		{
			return gpu;
		}

		const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const
		{
			return mem_props;
		}

		const VkPhysicalDeviceProperties& GetGPUProperties() const
		{
			return gpu_props;
		}

		const Sampler& GetStockSampler(StockSampler sampler) const;

	#ifdef QM_VULKAN_FILESYSTEM
		ShaderManager& get_shader_manager();
		TextureManager& get_texture_manager();
		void init_shader_manager_cache();
		void flush_shader_manager_cache();
	#endif

		// For some platforms, the device and queue might be shared, possibly across threads, so need some mechanism to
		// lock the global device and queue.
		void SetQueueLock(std::function<void()> lock_callback, std::function<void()> unlock_callback);

		const ImplementationWorkarounds& GetWorkarounds() const
		{
			return workarounds;
		}

		const DeviceFeatures& GetDeviceFeatures() const
		{
			return ext;
		}

		bool SwapchainTouched() const;

		double ConvertTimestampDelta(uint64_t start_ticks, uint64_t end_ticks) const;
		// Writes a timestamp on host side, which is calibrated to the GPU timebase.
		QueryPoolHandle WriteCalibratedTimestamp();

	private:
		VkInstance instance = VK_NULL_HANDLE;
		VkPhysicalDevice gpu = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		const VolkDeviceTable* table = nullptr;
		VkQueue graphics_queue = VK_NULL_HANDLE;
		VkQueue compute_queue = VK_NULL_HANDLE;
		VkQueue transfer_queue = VK_NULL_HANDLE;
		uint32_t timestamp_valid_bits = 0;
		unsigned num_thread_indices = 1;

	#ifdef GRANITE_VULKAN_MT
		std::atomic<uint64_t> cookie;
	#else
		uint64_t cookie = 0;
	#endif

		uint64_t allocate_cookie();
		void bake_program(Program& program);

		void request_vertex_block(BufferBlock& block, VkDeviceSize size);
		void request_index_block(BufferBlock& block, VkDeviceSize size);
		void request_uniform_block(BufferBlock& block, VkDeviceSize size);
		void request_staging_block(BufferBlock& block, VkDeviceSize size);

		QueryPoolHandle write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage);

		void set_acquire_semaphore(unsigned index, Semaphore acquire);
		Semaphore consume_release_semaphore();

		PipelineLayout* request_pipeline_layout(const CombinedResourceLayout& layout);
		DescriptorSetAllocator* request_descriptor_set_allocator(const DescriptorSetLayout& layout, const uint32_t* stages_for_sets);
		const Framebuffer& request_framebuffer(const RenderPassInfo& info);
		const RenderPass& request_render_pass(const RenderPassInfo& info, bool compatible);

		VkPhysicalDeviceMemoryProperties mem_props;
		VkPhysicalDeviceProperties gpu_props;

		DeviceFeatures ext;
		void init_stock_samplers();
		void init_timeline_semaphores();
		void init_bindless();
		void deinit_timeline_semaphores();

		struct JSONTraceFileDeleter { void operator()(FILE* file); };
		std::unique_ptr<FILE, JSONTraceFileDeleter> json_trace_file;
		int64_t json_base_timestamp_value = 0;
		int64_t json_timestamp_origin = 0;
		int64_t convert_timestamp_to_absolute_usec(uint64_t ts);
		uint64_t update_wrapped_base_timestamp(uint64_t ts);
		void write_json_timestamp_range(unsigned frame_index, const char* tid, const char* name, const char* extra,
										uint64_t start_ts, uint64_t end_ts,
										int64_t& min_us, int64_t& max_us);
		void write_json_timestamp_range_us(unsigned frame_index, const char* tid, const char* name, int64_t start_us, int64_t end_us);

		QueryPoolHandle write_timestamp_nolock(VkCommandBuffer cmd, VkPipelineStageFlagBits stage);
		QueryPoolHandle write_calibrated_timestamp_nolock();
		void register_time_interval_nolock(std::string tid, QueryPoolHandle start_ts, QueryPoolHandle end_ts, std::string tag, std::string extra);

		// Make sure this is deleted last.
		HandlePool handle_pool;

		// Calibrated timestamps.
		void init_calibrated_timestamps();
		void recalibrate_timestamps_fallback();
		void recalibrate_timestamps();
		bool resample_calibrated_timestamps();
		VkTimeDomainEXT calibrated_time_domain = VK_TIME_DOMAIN_DEVICE_EXT;
		int64_t calibrated_timestamp_device = 0;
		int64_t calibrated_timestamp_host = 0;
		int64_t last_calibrated_timestamp_host = 0; // To ensure monotonicity after a recalibration.
		unsigned timestamp_calibration_counter = 0;
		int64_t get_calibrated_timestamp();
		Vulkan::QueryPoolHandle frame_context_begin_ts;

		struct Managers
		{
			DeviceAllocator memory;
			FenceManager fence;
			SemaphoreManager semaphore;
			EventManager event;
			BufferPool vbo, ibo, ubo, staging;
			TimestampIntervalManager timestamps;
		};
		Managers managers;

		struct
		{
	#ifdef QM_VULKAN_MT
			std::mutex lock;
			std::condition_variable cond;
	#endif
			unsigned counter = 0;
		} lock;

		struct PerFrame
		{
			PerFrame(Device* device, unsigned index);
			~PerFrame();
			void operator=(const PerFrame&) = delete;
			PerFrame(const PerFrame&) = delete;

			void begin();

			Device& device;
			unsigned frame_index;
			const VolkDeviceTable& table;
			Managers& managers;
			std::vector<CommandPool> graphics_cmd_pool;
			std::vector<CommandPool> compute_cmd_pool;
			std::vector<CommandPool> transfer_cmd_pool;
			QueryPool query_pool;

			std::vector<BufferBlock> vbo_blocks;
			std::vector<BufferBlock> ibo_blocks;
			std::vector<BufferBlock> ubo_blocks;
			std::vector<BufferBlock> staging_blocks;

			VkSemaphore graphics_timeline_semaphore;
			VkSemaphore compute_timeline_semaphore;
			VkSemaphore transfer_timeline_semaphore;
			uint64_t timeline_fence_graphics = 0;
			uint64_t timeline_fence_compute = 0;
			uint64_t timeline_fence_transfer = 0;

			std::vector<VkFence> wait_fences;
			std::vector<VkFence> recycle_fences;

			std::vector<DeviceAllocation> allocations;
			std::vector<VkFramebuffer> destroyed_framebuffers;
			std::vector<VkSampler> destroyed_samplers;
			std::vector<VkPipeline> destroyed_pipelines;
			std::vector<VkImageView> destroyed_image_views;
			std::vector<VkBufferView> destroyed_buffer_views;
			std::vector<VkImage> destroyed_images;
			std::vector<VkBuffer> destroyed_buffers;
			std::vector<VkDescriptorPool> destroyed_descriptor_pools;
			Util::SmallVector<CommandBufferHandle> graphics_submissions;
			Util::SmallVector<CommandBufferHandle> compute_submissions;
			Util::SmallVector<CommandBufferHandle> transfer_submissions;
			std::vector<VkSemaphore> recycled_semaphores;
			std::vector<VkEvent> recycled_events;
			std::vector<VkSemaphore> destroyed_semaphores;
			std::vector<ImageHandle> keep_alive_images;

			struct DebugChannel
			{
				DebugChannelInterface* iface;
				std::string tag;
				BufferHandle buffer;
			};
			std::vector<DebugChannel> debug_channels;

			struct TimestampIntervalHandles
			{
				std::string tid;
				QueryPoolHandle start_ts;
				QueryPoolHandle end_ts;
				TimestampInterval* timestamp_tag;
				std::string extra;
			};
			std::vector<TimestampIntervalHandles> timestamp_intervals;

			bool in_destructor = false;
		};
		// The per frame structure must be destroyed after
		// the hashmap data structures below, so it must be declared before.
		std::vector<std::unique_ptr<PerFrame>> per_frame;

		struct
		{
			Semaphore acquire;
			Semaphore release;
			bool touched = false;
			bool consumed = false;
			std::vector<ImageHandle> swapchain;
			unsigned index = 0;
		} wsi;

		struct QueueData
		{
			Util::SmallVector<Semaphore> wait_semaphores;
			Util::SmallVector<VkPipelineStageFlags> wait_stages;
			bool need_fence = false;

			VkSemaphore timeline_semaphore = VK_NULL_HANDLE;
			uint64_t current_timeline = 0;
			PerformanceQueryPool performance_query_pool;
		} graphics, compute, transfer;

		struct InternalFence
		{
			VkFence fence;
			VkSemaphore timeline;
			uint64_t value;
		};

		// Pending buffers which need to be copied from CPU to GPU before submitting graphics or compute work.
		struct
		{
			std::vector<BufferBlock> vbo;
			std::vector<BufferBlock> ibo;
			std::vector<BufferBlock> ubo;
		} dma;

		void submit_queue(CommandBuffer::Type type, InternalFence* fence,
						  unsigned semaphore_count = 0,
						  Semaphore* semaphore = nullptr,
						  int profiled_iteration = -1);

		PerFrame& frame()
		{
			VK_ASSERT(frame_context_index < per_frame.size());
			VK_ASSERT(per_frame[frame_context_index]);
			return *per_frame[frame_context_index];
		}

		const PerFrame& frame() const
		{
			VK_ASSERT(frame_context_index < per_frame.size());
			VK_ASSERT(per_frame[frame_context_index]);
			return *per_frame[frame_context_index];
		}

		unsigned frame_context_index = 0;
		uint32_t graphics_queue_family_index = 0;
		uint32_t compute_queue_family_index = 0;
		uint32_t transfer_queue_family_index = 0;

		uint32_t find_memory_type(BufferDomain domain, uint32_t mask);
		uint32_t find_memory_type(ImageDomain domain, uint32_t mask);
		bool memory_type_is_device_optimal(uint32_t type) const;
		bool memory_type_is_host_visible(uint32_t type) const;

		SamplerHandle samplers[static_cast<unsigned>(StockSampler::Count)];
		VkSamplerYcbcrConversion samplers_ycbcr[static_cast<unsigned>(YCbCrFormat::Count)] = {};

		VulkanCache<PipelineLayout> pipeline_layouts;
		VulkanCache<DescriptorSetAllocator> descriptor_set_allocators;
		VulkanCache<RenderPass> render_passes;
		VulkanCache<Shader> shaders;
		VulkanCache<Program> programs;

		DescriptorSetAllocator* bindless_sampled_image_allocator_fp = nullptr;
		DescriptorSetAllocator* bindless_sampled_image_allocator_integer = nullptr;

		FramebufferAllocator framebuffer_allocator;
		TransientAttachmentAllocator transient_allocator;
		VkPipelineCache pipeline_cache = VK_NULL_HANDLE;

		SamplerHandle create_sampler(const SamplerCreateInfo& info, StockSampler sampler);
		void init_pipeline_cache();
		void flush_pipeline_cache();

		CommandPool& get_command_pool(CommandBuffer::Type type, unsigned thread);
		QueueData& get_queue_data(CommandBuffer::Type type);
		VkQueue get_vk_queue(CommandBuffer::Type type) const;
		PerformanceQueryPool& get_performance_query_pool(CommandBuffer::Type type);
		Util::SmallVector<CommandBufferHandle>& get_queue_submissions(CommandBuffer::Type type);
		void clear_wait_semaphores();
		void submit_staging(CommandBufferHandle& cmd, VkBufferUsageFlags usage, bool flush);
		PipelineEvent request_pipeline_event();

		std::function<void()> queue_lock_callback;
		std::function<void()> queue_unlock_callback;
		void flush_frame(CommandBuffer::Type type);
		void sync_buffer_blocks();
		void submit_empty_inner(CommandBuffer::Type type, InternalFence* fence,
								unsigned semaphore_count,
								Semaphore* semaphore);

		void destroy_buffer(VkBuffer buffer);
		void destroy_image(VkImage image);
		void destroy_image_view(VkImageView view);
		void destroy_buffer_view(VkBufferView view);
		void destroy_pipeline(VkPipeline pipeline);
		void destroy_sampler(VkSampler sampler);
		void destroy_framebuffer(VkFramebuffer framebuffer);
		void destroy_semaphore(VkSemaphore semaphore);
		void recycle_semaphore(VkSemaphore semaphore);
		void destroy_event(VkEvent event);
		void free_memory(const DeviceAllocation& alloc);
		void reset_fence(VkFence fence, bool observed_wait);
		void keep_handle_alive(ImageHandle handle);
		void destroy_descriptor_pool(VkDescriptorPool desc_pool);

		void destroy_buffer_nolock(VkBuffer buffer);
		void destroy_image_nolock(VkImage image);
		void destroy_image_view_nolock(VkImageView view);
		void destroy_buffer_view_nolock(VkBufferView view);
		void destroy_pipeline_nolock(VkPipeline pipeline);
		void destroy_sampler_nolock(VkSampler sampler);
		void destroy_framebuffer_nolock(VkFramebuffer framebuffer);
		void destroy_semaphore_nolock(VkSemaphore semaphore);
		void recycle_semaphore_nolock(VkSemaphore semaphore);
		void destroy_event_nolock(VkEvent event);
		void free_memory_nolock(const DeviceAllocation& alloc);
		void destroy_descriptor_pool_nolock(VkDescriptorPool desc_pool);
		void reset_fence_nolock(VkFence fence, bool observed_wait);

		void flush_frame_nolock();
		CommandBufferHandle request_command_buffer_nolock(unsigned thread_index, CommandBuffer::Type type, bool profiled);
		void submit_nolock(CommandBufferHandle cmd, Fence* fence,
						   unsigned semaphore_count, Semaphore* semaphore);
		void submit_empty_nolock(CommandBuffer::Type type, Fence* fence,
								 unsigned semaphore_count,
								 Semaphore* semaphore, int profiling_iteration);
		void add_wait_semaphore_nolock(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages,
									   bool flush);

		void request_vertex_block_nolock(BufferBlock& block, VkDeviceSize size);
		void request_index_block_nolock(BufferBlock& block, VkDeviceSize size);
		void request_uniform_block_nolock(BufferBlock& block, VkDeviceSize size);
		void request_staging_block_nolock(BufferBlock& block, VkDeviceSize size);

		CommandBufferHandle request_secondary_command_buffer_for_thread(unsigned thread_index,
																		const Framebuffer* framebuffer,
																		unsigned subpass,
																		CommandBuffer::Type type = CommandBuffer::Type::Generic);
		void add_frame_counter_nolock();
		void decrement_frame_counter_nolock();
		void submit_secondary(CommandBuffer& primary, CommandBuffer& secondary);
		void wait_idle_nolock();
		void end_frame_nolock();

		void add_debug_channel_buffer(DebugChannelInterface* iface, std::string tag, BufferHandle buffer);
		void parse_debug_channel(const PerFrame::DebugChannel& channel);

		Fence request_legacy_fence();

	#ifdef QM_VULKAN_FILESYSTEM
		ShaderManager shader_manager;
		TextureManager texture_manager;
	#endif

		std::string get_pipeline_cache_string() const;

	#ifdef QM_VULKAN_FOSSILIZE
		Fossilize::StateRecorder state_recorder;
		bool enqueue_create_sampler(Fossilize::Hash hash, const VkSamplerCreateInfo* create_info, VkSampler* sampler) override;
		bool enqueue_create_descriptor_set_layout(Fossilize::Hash hash, const VkDescriptorSetLayoutCreateInfo* create_info, VkDescriptorSetLayout* layout) override;
		bool enqueue_create_pipeline_layout(Fossilize::Hash hash, const VkPipelineLayoutCreateInfo* create_info, VkPipelineLayout* layout) override;
		bool enqueue_create_shader_module(Fossilize::Hash hash, const VkShaderModuleCreateInfo* create_info, VkShaderModule* module) override;
		bool enqueue_create_render_pass(Fossilize::Hash hash, const VkRenderPassCreateInfo* create_info, VkRenderPass* render_pass) override;
		bool enqueue_create_compute_pipeline(Fossilize::Hash hash, const VkComputePipelineCreateInfo* create_info, VkPipeline* pipeline) override;
		bool enqueue_create_graphics_pipeline(Fossilize::Hash hash, const VkGraphicsPipelineCreateInfo* create_info, VkPipeline* pipeline) override;
		void notify_replayed_resources_for_type() override;
		VkPipeline fossilize_create_graphics_pipeline(Fossilize::Hash hash, VkGraphicsPipelineCreateInfo& info);
		VkPipeline fossilize_create_compute_pipeline(Fossilize::Hash hash, VkComputePipelineCreateInfo& info);

		void register_graphics_pipeline(Fossilize::Hash hash, const VkGraphicsPipelineCreateInfo& info);
		void register_compute_pipeline(Fossilize::Hash hash, const VkComputePipelineCreateInfo& info);
		void register_render_pass(VkRenderPass render_pass, Fossilize::Hash hash, const VkRenderPassCreateInfo& info);
		void register_descriptor_set_layout(VkDescriptorSetLayout layout, Fossilize::Hash hash, const VkDescriptorSetLayoutCreateInfo& info);
		void register_pipeline_layout(VkPipelineLayout layout, Fossilize::Hash hash, const VkPipelineLayoutCreateInfo& info);
		void register_shader_module(VkShaderModule module, Fossilize::Hash hash, const VkShaderModuleCreateInfo& info);
		void register_sampler(VkSampler sampler, Fossilize::Hash hash, const VkSamplerCreateInfo& info);

		struct
		{
			std::unordered_map<VkShaderModule, Shader*> shader_map;
			std::unordered_map<VkRenderPass, RenderPass*> render_pass_map;
	#ifdef GRANITE_VULKAN_MT
			Granite::TaskGroup pipeline_group;
	#endif
		} replayer_state;

		void init_pipeline_state();
		void flush_pipeline_state();
	#endif

		ImplementationWorkarounds workarounds;
		void init_workarounds();
		void report_checkpoints();

		void fill_buffer_sharing_indices(VkBufferCreateInfo& create_info, uint32_t* sharing_indices);

		bool allocate_image_memory(DeviceAllocation* allocation, const ImageCreateInfo& info,
								   VkImage image, VkImageTiling tiling);
	};

}
