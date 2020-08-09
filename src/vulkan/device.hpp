#pragma once

#include "vulkan_headers.hpp"
#include "vulkan_common.hpp"

#include "command_buffer.hpp"
#include "command_pool.hpp"
#include "context.hpp"

#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <stdio.h>
#include <utility>

#include "memory/buffer.hpp"
#include "memory/buffer_pool.hpp"
#include "memory/memory_allocator.hpp"

#include "images/image.hpp"
#include "images/sampler.hpp"

#include "graphics/render_pass.hpp"
#include "graphics/shader.hpp"

#include "sync/fence.hpp"
#include "sync/fence_manager.hpp"
#include "sync/semaphore.hpp"
#include "sync/semaphore_manager.hpp"
#include "sync/pipeline_event.hpp"
#include "sync/event_manager.hpp"

#ifdef QM_VULKAN_MT
#include <atomic>
#include <mutex>
#include <condition_variable>
#endif

#include "fossilize.hpp"

#include "threading/thread_group.hpp"

#include "misc/quirks.hpp"
#include "utils/small_vector.hpp"
#include "utils/retained_heap_data.hpp"

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

	//Object containing all of the object pools that device uses to allocate handles
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
		VulkanObjectPool<CommandBuffer> command_buffers;
		VulkanObjectPool<BindlessDescriptorPool> bindless_descriptor_pool;
	};

	struct FossilizeReplayer
	{
		std::unordered_map<VkShaderModule, Shader*> shader_map;
		std::unordered_map<VkRenderPass, RenderPass*> render_pass_map;
#ifdef QM_VULKAN_MT
		Quantum::TaskGroup pipeline_group;
#endif
	};

	// Pending buffers which need to be copied from CPU to GPU before submitting graphics or compute work.
	struct DmaQueues
	{
		std::vector<BufferBlock> vbo;
		std::vector<BufferBlock> ibo;
		std::vector<BufferBlock> ubo;
	};

	struct WSIData
	{
		Semaphore acquire;
		Semaphore release;
		bool touched = false;
		bool consumed = false;
		std::vector<ImageHandle> swapchain;
		unsigned index = 0;
	};

	//Contains data about a queue
	struct QueueData
	{
		Util::SmallVector<Semaphore> wait_semaphores;
		Util::SmallVector<VkPipelineStageFlags> wait_stages;
		bool need_fence = false;

		VkSemaphore timeline_semaphore = VK_NULL_HANDLE;
		uint64_t current_timeline = 0;
	};

	//Fence used internally by device
	struct InternalFence
	{
		VkFence fence;
		VkSemaphore timeline;
		uint64_t value;
	};

	//Various manager classes the device uses
	struct DeviceManagers
	{
		DeviceAllocator memory;
		FenceManager fence;
		SemaphoreManager semaphore;
		EventManager event;
		BufferPool vbo, ibo, ubo, staging;
		//TimestampIntervalManager timestamps;
	};

	struct DeviceLock
	{
#ifdef QM_VULKAN_MT
		std::mutex lock;
		std::condition_variable cond;
#endif
		unsigned counter = 0;
	};

	struct PerFrame
	{
		PerFrame(Device* device, unsigned index);
		~PerFrame();
		void operator=(const PerFrame&) = delete;
		PerFrame(const PerFrame&) = delete;

		void Begin();

		//Reference to device
		Device& device;
		//Frame index
		unsigned frame_index;
		//Reference to device table
		const VolkDeviceTable& table;
		//Reference to managers
		DeviceManagers& managers;
		std::vector<CommandPool> graphics_cmd_pool;
		std::vector<CommandPool> compute_cmd_pool;
		std::vector<CommandPool> transfer_cmd_pool;

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

		std::vector<VkFramebuffer> destroyed_framebuffers;
		std::vector<VkSampler> destroyed_samplers;
		std::vector<VkPipeline> destroyed_pipelines;
		std::vector<VkImageView> destroyed_image_views;
		std::vector<VkBufferView> destroyed_buffer_views;
		std::vector<std::pair<VkImage, DeviceAllocation>> destroyed_images;
		std::vector<std::pair<VkBuffer, DeviceAllocation>> destroyed_buffers;
		std::vector<VkDescriptorPool> destroyed_descriptor_pools;
		Util::SmallVector<CommandBufferHandle> graphics_submissions;
		Util::SmallVector<CommandBufferHandle> compute_submissions;
		Util::SmallVector<CommandBufferHandle> transfer_submissions;
		std::vector<VkSemaphore> recycled_semaphores;
		std::vector<VkEvent> recycled_events;
		std::vector<VkSemaphore> destroyed_semaphores;
		std::vector<ImageHandle> keep_alive_images;
	};

	class Device : public Fossilize::StateCreatorInterface
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
		friend struct PerFrame;

		Device();
		~Device();

		// No move-copy.
		void operator=(Device&&) = delete;
		Device(Device&&) = delete;

		// Only called by main thread, during setup phase.
		// Sets context and initializes device
		void SetContext(const ContextHandle& context, uint8_t* initial_cache_data, size_t initial_cache_size, uint8_t* fossilize_pipeline_data, size_t fossilize_pipeline_size);

		void InitSwapchain(const std::vector<VkImage>& swapchain_images, unsigned width, unsigned height, VkFormat format);
		void InitExternalSwapchain(const std::vector<ImageHandle>& swapchain_images);
		// Creates the frame contexts. This is automatically called by SetContext(). This implementation defaults to 2 frames, but this command can be called to change that
		void InitFrameContexts(unsigned count);

		// Returns the current image view
		ImageView& GetSwapchainView();
		// Returns the swapchain view of a particular index
		ImageView& GetSwapchainView(unsigned index);
		// Returns the size of the swapchain
		unsigned GetNumSwapchainImages() const;
		// Returns the number of frame contexts
		unsigned GetNumFrameContexts() const;
		// Returns the current swapchain index
		unsigned GetSwapchainIndex() const;
		// Returns the current frame context index
		unsigned GetCurrentFrameContext() const;

		// Retrieves the pipeline cache data. This should be stored in a file (before device is destroyed) by the client and loaded up in SetContext.
		Util::RetainedHeapData GetPipelineCacheData(size_t override_max_size = 0);
		// Retrieves fossilize pipeline data.
		Util::RetainedHeapData GetFossilizePipelineData();

		// Frame-pushing interface.

		// Move to the next frame context
		void NextFrameContext();
		void WaitIdle();
		void EndFrameContext();

		// Submission interface, may be called from any thread at any time.

		// Make sure all pending submits to the current frame are processed
		void FlushFrame();

		// Command buffers are transient in Granite.
		// Once you request a command buffer you must submit it in the current frame context before moving to the next one.
		// More detailed examples of command buffers will follow in future samples.
		// There are different command buffer types which correspond to general purpose queue, async compute, DMA queue, etc.
		// Generic is the default, and the argument can be omitted.

		// Returns a new command buffer
		CommandBufferHandle RequestCommandBuffer(CommandBuffer::Type type = CommandBuffer::Type::Generic);
		// Returns a command buffer for a specific thread. Thread_index must be less than the context's num thread indices
		CommandBufferHandle RequestCommandBufferForThread(unsigned thread_index, CommandBuffer::Type type = CommandBuffer::Type::Generic);
		// Submits a command to be executed. semaphore is an array of semaphores that will be filled with 
		// signal semaphores (aka semaphores that will cause other commands to wait until submission is complete)
		void Submit(CommandBufferHandle& cmd, Fence* fence = nullptr, unsigned semaphore_count = 0, Semaphore* semaphore = nullptr);
		void SubmitEmpty(CommandBuffer::Type type,  Fence* fence = nullptr,unsigned semaphore_count = 0, Semaphore* semaphore = nullptr);
		//Adds a wait semaphore and wait stages to the next queue submit of a certain type
		void AddWaitSemaphore(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush);
		CommandBuffer::Type GetPhysicalQueueType(CommandBuffer::Type queue_type) const;

		// "Requests" essentially hash the object, check if it exists in the cache and if not creates a new object

		// Creates a shader with code and size. If shader has already been created this just returns that
		Shader* RequestShader(const uint32_t* code, size_t size);
		// Requests an already created shader using its hash
		Shader* RequestShaderByHash(Util::Hash hash);
		// Creates a program with a vertex sahder and fragment shader. Also hashing
		Program* RequestProgram(const uint32_t* vertex_data, size_t vertex_size, const uint32_t* fragment_data, size_t fragment_size);
		// Creates a program with a compute shader. Also hashing.
		Program* RequestProgram(const uint32_t* compute_data, size_t compute_size);
		// Creates a program from a vertex and fragment shader
		Program* RequestProgram(Shader* vertex, Shader* fragment);
		// Creates a progam from a compute shader
		Program* RequestProgram(Shader* compute);

		// Map and unmap buffer objects.
		void* MapHostBuffer(const Buffer& buffer, MemoryAccessFlags access);
		void UnmapHostBuffer(const Buffer& buffer, MemoryAccessFlags access);

		// Map Linear Host image
		void* MapLinearHostImage(const LinearHostImage& image, MemoryAccessFlags access);
		void UnmapLinearHostImageAndSync(const LinearHostImage& image, MemoryAccessFlags access);

		//Return whether allocation has certain memory flags
		bool AllocationHasMemoryPropertyFlags(DeviceAllocation& alloc, VkMemoryPropertyFlags flags)
		{
			return HasMemoryPropertyFlags(alloc, mem_props, flags);
		}

		// Creates and allocates a buffer and images.
		BufferHandle CreateBuffer(const BufferCreateInfo& info, const void* initial = nullptr);
		// Creates and allocates an image
		ImageHandle CreateImage(const ImageCreateInfo& info, const ImageInitialData* initial = nullptr);
		// Creates an image using a staging buffer
		ImageHandle CreateImageFromStagingBuffer(const ImageCreateInfo& info, const InitialImageBuffer* buffer);
		// Essentially an image that can be sampled on the GPU as a vk image, but it also has a vkbuffer conterpart on the cpu
		LinearHostImageHandle CreateLinearHostImage(const LinearHostImageCreateInfo& info);

		// Create staging buffers for images.
		InitialImageBuffer CreateImageStagingBuffer(const ImageCreateInfo& info, const ImageInitialData* initial);
		InitialImageBuffer CreateImageStagingBuffer(const TextureFormatLayout& layout);

		// Create image view
		ImageViewHandle CreateImageView(const ImageViewCreateInfo& view_info);
		// Create buffer view
		BufferViewHandle CreateBufferView(const BufferViewCreateInfo& view_info);
		// Create sampler
		SamplerHandle CreateSampler(const SamplerCreateInfo& info);

		BindlessDescriptorPoolHandle CreateBindlessDescriptorPool(BindlessResourceType type, unsigned num_sets, unsigned num_descriptors);

		// Detects whether a given format supports the format fetures
		bool ImageFormatIsSupported(VkFormat format, VkFormatFeatureFlags required, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL) const;
		// Retrieves the properties of a format
		void GetFormatProperties(VkFormat format, VkFormatProperties* properties);
		// Retrieves the image format properties of an image
		bool GetImageFormatProperties(VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* properties);
		// Returns the defualt depth stencil format
		VkFormat GetDefaultDepthStencilFormat() const;
		// Returns the default depth format
		VkFormat GetDefaultDepthFormat() const;
		// Returns a transiant attachment
		ImageView& GetTransientAttachment(unsigned width, unsigned height, VkFormat format, unsigned index = 0, unsigned samples = 1, unsigned layers = 1);
		// Gets the renderpassinfo from a SwapchainRenderPass enum
		RenderPassInfo GetSwapchainRenderPass(SwapchainRenderPass style);

		// Timeline semaphores are only used internally to reduce handle bloat.
		// Creates and returns a non timeline semaphore
		Semaphore RequestLegacySemaphore();
		// Turns an externally created semaphore into a QM semaphore. signalled controls whether semaphore is initially signalled or not.
		Semaphore RequestExternalSemaphore(VkSemaphore semaphore, bool signalled);
		// Get the vulkan instance
		VkInstance GetInstance() const
		{
			return instance;
		}
		// Get the vulkan device (interface with gpu)
		VkDevice GetDevice() const
		{
			return device;
		}
		// Get the vulkan physical device (the gpu)
		VkPhysicalDevice GetPhysicalDevice() const
		{
			return gpu;
		}
		// Get the memory properties of the device
		const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const
		{
			return mem_props;
		}
		// Get the physical properties of the device
		const VkPhysicalDeviceProperties& GetGPUProperties() const
		{
			return gpu_props;
		}
		// Get the volk table (vulkan function loader)
		const VolkDeviceTable& GetDeviceTable() const
		{
			return *table;
		}
		// Get a stock sampler (basically common sampler types held in the StockSampler enum)
		const Sampler& GetStockSampler(StockSampler sampler) const;

		// For some platforms, the device and queue might be shared, possibly across threads, so need some mechanism to
		// lock the global device and queue.

		// Set call backs for before and after vkQueueSubmit calls
		void SetQueueLock(std::function<void()> lock_callback, std::function<void()> unlock_callback);
		// Get enabled workarounds
		const ImplementationWorkarounds& GetWorkarounds() const
		{
			return workarounds;
		}
		// Get enabled device fetures
		const DeviceFeatures& GetDeviceFeatures() const
		{
			return *ext;
		}
		// Return whether the swapchain has been used in this frame
		bool SwapchainTouched() const;

	private:
		//Hold on to a reference to context
		ContextHandle context;

		VkInstance instance = VK_NULL_HANDLE;
		VkPhysicalDevice gpu = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		const VolkDeviceTable* table = nullptr;
		VkQueue graphics_queue = VK_NULL_HANDLE;
		VkQueue compute_queue = VK_NULL_HANDLE;
		VkQueue transfer_queue = VK_NULL_HANDLE;
		uint32_t timestamp_valid_bits = 0;
		unsigned num_thread_indices = 1;

	#ifdef QM_VULKAN_MT
		std::atomic<uint64_t> cookie;
	#else
		uint64_t cookie = 0;
	#endif

		uint64_t AllocateCookie();
		void BakeProgram(Program& program);

		void RequestVertexBlock(BufferBlock& block, VkDeviceSize size);
		void RequestIndexBlock(BufferBlock& block, VkDeviceSize size);
		void RequestUniformBlock(BufferBlock& block, VkDeviceSize size);
		void RequestStagingBlock(BufferBlock& block, VkDeviceSize size);

		void SetAcquireSemaphore(unsigned index, Semaphore acquire);
		Semaphore ConsumeReleaseSemaphore();

		PipelineLayout* RequestPipelineLayout(const CombinedResourceLayout& layout);
		DescriptorSetAllocator* RequestDescriptorSetAllocator(const DescriptorSetLayout& layout, const uint32_t* stages_for_sets);
		const Framebuffer& RequestFramebuffer(const RenderPassInfo& info);
		const RenderPass& RequestRenderPass(const RenderPassInfo& info, bool compatible);

		VkPhysicalDeviceMemoryProperties mem_props;
		VkPhysicalDeviceProperties gpu_props;

		const DeviceFeatures* ext;
		//Creates every type of stock sampler (by creating 1 sampler for each enum type)
		void InitStockSamplers();
		void InitTimelineSemaphores();
		void InitBindless();
		void DeinitTimelineSemaphores();

		// Make sure this is deleted last.
		HandlePool handle_pool;

		DeviceManagers managers;

#ifdef QM_VULKAN_MT
		Quantum::ThreadGroup thread_group;
		std::mutex thread_group_mutex;
#endif

		DeviceLock lock;

		// The per frame structure must be destroyed after
		// the hashmap data structures below, so it must be declared before.
		std::vector<std::unique_ptr<PerFrame>> per_frame;

		WSIData wsi;

		QueueData graphics, compute, transfer;

		// Pending buffers which need to be copied from CPU to GPU before submitting graphics or compute work.
		DmaQueues dma;
		//Flush all pending submission to a certain queue type. 
		void SubmitQueue(CommandBuffer::Type type, InternalFence* fence,  unsigned semaphore_count = 0, Semaphore* semaphore = nullptr);
		//Return the current PerFrame object
		PerFrame& Frame()
		{
			VK_ASSERT(frame_context_index < per_frame.size());
			VK_ASSERT(per_frame[frame_context_index]);
			return *per_frame[frame_context_index];
		}

		const PerFrame& Frame() const
		{
			VK_ASSERT(frame_context_index < per_frame.size());
			VK_ASSERT(per_frame[frame_context_index]);
			return *per_frame[frame_context_index];
		}

		unsigned frame_context_index = 0;
		uint32_t graphics_queue_family_index = 0;
		uint32_t compute_queue_family_index = 0;
		uint32_t transfer_queue_family_index = 0;

		SamplerHandle samplers[static_cast<unsigned>(StockSampler::Count)];

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

		SamplerHandle CreateSampler(const SamplerCreateInfo& info, StockSampler sampler);
		bool InitPipelineCache(const uint8_t* initial_cache_data, size_t initial_cache_size);
		bool InitFossilizePipeline(const uint8_t* fossilize_pipeline_data, size_t fossilize_pipeline_size);

		CommandPool& GetCommandPool(CommandBuffer::Type type, unsigned thread);
		QueueData& GetQueueData(CommandBuffer::Type type);
		VkQueue GetVkQueue(CommandBuffer::Type type) const;
		Util::SmallVector<CommandBufferHandle>& GetQueueSubmission(CommandBuffer::Type type);
		void ClearWaitSemaphores();
		//Submit staging buffer commands (basically just vkCmdCopyBuffers) and ensures no queue will use those resources until this submission is compelete
		void SubmitStaging(CommandBufferHandle& cmd, VkBufferUsageFlags usage, bool flush);
		PipelineEvent RequestPipelineEvent();

		std::function<void()> queue_lock_callback;
		std::function<void()> queue_unlock_callback;

		//Flushs all pending submission of a certain type for the current frame
		void FlushFrame(CommandBuffer::Type type);
		//Flushes all pending DMA staging writes
		void SyncBufferBlocks();
		void SubmitEmptyInner(CommandBuffer::Type type, InternalFence* fence, unsigned semaphore_count, Semaphore* semaphore);

		void DestroyBuffer(VkBuffer buffer, const DeviceAllocation& allocation);
		void DestroyImage(VkImage image, const DeviceAllocation& allocation);
		void DestroyImageView(VkImageView view);
		void DestroyBufferView(VkBufferView view);
		void DestroyPipeline(VkPipeline pipeline);
		void DestroySampler(VkSampler sampler);
		void DestroyFramebuffer(VkFramebuffer framebuffer);
		void DestroySemaphore(VkSemaphore semaphore);
		void RecycleSemaphore(VkSemaphore semaphore);
		void DestroyEvent(VkEvent event);
		void ResetFence(VkFence fence, bool observed_wait);
		void KeepHandleAlive(ImageHandle handle);
		void DestroyDescriptorPool(VkDescriptorPool desc_pool);

		void DestroyBufferNolock(VkBuffer buffer, const DeviceAllocation& allocation);
		void DestroyImageNolock(VkImage image, const DeviceAllocation& allocation);
		void DestroyImageViewNolock(VkImageView view);
		void DestroyBufferViewNolock(VkBufferView view);
		void DestroyPipelineNolock(VkPipeline pipeline);
		void DestroySamplerNolock(VkSampler sampler);
		void DestroyFramebufferNolock(VkFramebuffer framebuffer);
		void DestroySemaphoreNolock(VkSemaphore semaphore);
		void RecycleSemaphoreNolock(VkSemaphore semaphore);
		void DestroyEventNolock(VkEvent event);
		void DestroyDescriptorPoolNolock(VkDescriptorPool desc_pool);
		void ResetFenceNolock(VkFence fence, bool observed_wait);

		void FlushFrameNolock();
		CommandBufferHandle RequestCommandBufferNolock(unsigned thread_index, CommandBuffer::Type type, bool profiled);
		//Ends command buffer. If there is a fence or semaphore to signal, this submits to queue immediately, otherwise deffer submission.
		void SubmitNolock(CommandBufferHandle cmd, Fence* fence, unsigned semaphore_count, Semaphore* semaphore);
		void SubmitEmptyNolock(CommandBuffer::Type type, Fence* fence, unsigned semaphore_count, Semaphore* semaphore);
		void AddWaitSemaphoreNolock(CommandBuffer::Type type, Semaphore semaphore, VkPipelineStageFlags stages, bool flush);

		void RequestVertexBlockNolock(BufferBlock& block, VkDeviceSize size);
		void RequestIndexBlockNolock(BufferBlock& block, VkDeviceSize size);
		void RequestUniformBlockNolock(BufferBlock& block, VkDeviceSize size);
		void RequestStagingBlockNolock(BufferBlock& block, VkDeviceSize size);

		CommandBufferHandle RequestSecondaryCommandBufferForThread(unsigned thread_index, const Framebuffer* framebuffer, unsigned subpass, CommandBuffer::Type type = CommandBuffer::Type::Generic);
		void AddFrameCounterNolock();
		void DecrementFrameCounterNolock();
		void SubmitSecondary(CommandBuffer& primary, CommandBuffer& secondary);
		void WaitIdleNolock();
		void EndFrameNolock();

		Fence RequestLegacyFence();

	#ifdef QM_VULKAN_FILESYSTEM
		ShaderManager shader_manager;
		TextureManager texture_manager;
	#endif

		Fossilize::StateRecorder state_recorder;
		//Create sampler with hash
		bool enqueue_create_sampler(Fossilize::Hash hash, const VkSamplerCreateInfo* create_info, VkSampler* sampler) override;
		//Emmits dummy index
		bool enqueue_create_descriptor_set_layout(Fossilize::Hash hash, const VkDescriptorSetLayoutCreateInfo* create_info, VkDescriptorSetLayout* layout) override;
		//Emmits dummy index
		bool enqueue_create_pipeline_layout(Fossilize::Hash hash, const VkPipelineLayoutCreateInfo* create_info, VkPipelineLayout* layout) override;
		//Create shader module with hash
		bool enqueue_create_shader_module(Fossilize::Hash hash, const VkShaderModuleCreateInfo* create_info, VkShaderModule* module) override;
		//Create render_pass with hash
		bool enqueue_create_render_pass(Fossilize::Hash hash, const VkRenderPassCreateInfo* create_info, VkRenderPass* render_pass) override;
		//Same as enqueue_create_graphics_pipeline but with compute pipelines
		bool enqueue_create_compute_pipeline(Fossilize::Hash hash, const VkComputePipelineCreateInfo* create_info, VkPipeline* pipeline) override;
		//If multithreading is enabled this queues fossilize_create_graphics_pipeline on another thread
		bool enqueue_create_graphics_pipeline(Fossilize::Hash hash, const VkGraphicsPipelineCreateInfo* create_info, VkPipeline* pipeline) override;
		void notify_replayed_resources_for_type() override;
		//Create graphics pipeline using fossilize cache
		VkPipeline fossilize_create_graphics_pipeline(Fossilize::Hash hash, VkGraphicsPipelineCreateInfo& info);
		//Create compute pipeline using fossilize cache
		VkPipeline fossilize_create_compute_pipeline(Fossilize::Hash hash, VkComputePipelineCreateInfo& info);

		//Resgiesters a graphics pipeline to fossilize to be cached
		void register_graphics_pipeline(Fossilize::Hash hash, const VkGraphicsPipelineCreateInfo& info);
		//Resgiesters a comput pipeline to fossilize to be cached
		void register_compute_pipeline(Fossilize::Hash hash, const VkComputePipelineCreateInfo& info);
		//Resgiesters a render pass to fossilize to be cached
		void register_render_pass(VkRenderPass render_pass, Fossilize::Hash hash, const VkRenderPassCreateInfo& info);
		//Resgiesters a descriptor set layout to fossilize to be cached
		void register_descriptor_set_layout(VkDescriptorSetLayout layout, Fossilize::Hash hash, const VkDescriptorSetLayoutCreateInfo& info);
		//Resgiesters a pipelinelayout to fossilize to be cached
		void register_pipeline_layout(VkPipelineLayout layout, Fossilize::Hash hash, const VkPipelineLayoutCreateInfo& info);
		//Resgiesters a shader module to fossilize to be cached
		void register_shader_module(VkShaderModule module, Fossilize::Hash hash, const VkShaderModuleCreateInfo& info);
		//Resgiesters a sampler to fossilize to be cached
		void register_sampler(VkSampler sampler, Fossilize::Hash hash, const VkSamplerCreateInfo& info);

		FossilizeReplayer replayer_state;

		ImplementationWorkarounds workarounds;
		void InitWorkarounds();

		void FillBufferSharingIndices(VkBufferCreateInfo& create_info, uint32_t* sharing_indices);
	};

}
