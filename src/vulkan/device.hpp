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
		//VulkanObjectPool<BindlessDescriptorPool> bindless_descriptor_pool;
		VulkanObjectPool<Shader> shaders;
		VulkanObjectPool<Program> programs;
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
		// Global lock, syncing most of Device's funtionalities
		std::mutex lock;
		std::condition_variable cond;

		// Program lock, managing program and shader deletion
		std::mutex program_lock;
		std::mutex shader_lock;
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
		std::vector<VkImageView> destroyed_image_views;
		std::vector<VkBufferView> destroyed_buffer_views;
		std::vector<std::pair<VkImage, DeviceAllocation>> destroyed_images;
		std::vector<std::pair<VkBuffer, DeviceAllocation>> destroyed_buffers;

		std::vector<Program*> destroyed_programs;
		std::vector<Shader*> destroyed_shaders;

		std::vector<ImageHandle> keep_alive_images;

		Util::SmallVector<CommandBufferHandle> graphics_submissions;
		Util::SmallVector<CommandBufferHandle> compute_submissions;
		Util::SmallVector<CommandBufferHandle> transfer_submissions;
		std::vector<VkSemaphore> recycled_semaphores;
		std::vector<VkEvent> recycled_events;
		std::vector<VkSemaphore> destroyed_semaphores;
	};

	class Device;

	class Device
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
		friend class Shader;
		friend struct ShaderDeleter;
		friend class Program;
		friend struct ProgramDeleter;
		friend class ProgramLayout;
		friend class WSI;
		friend class Cookie;
		friend class Framebuffer;
		friend class FramebufferAllocator;
		friend class RenderPass;
		friend class Texture;
		friend class DescriptorSetAllocator;
		friend class ImageResourceHolder;
		friend struct PerFrame;

		Device();
		~Device();

		// No move-copy.
		void operator=(Device&&) = delete;
		Device(Device&&) = delete;

		// Only called by main thread, during setup phase.
		// Sets context and initializes device
		void SetContext(Context* context, uint8_t* initial_cache_data, size_t initial_cache_size);

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

		// Creates a new shader using spirv code. Code is stored in 4 byte words. Size variable is the size of the code in bytes
		ShaderHandle CreateShader(const uint32_t* code, size_t size);
		// Creates a shader from glsl code. Converts glsl to spirv using glslang library. Adds user defined include directories for #include statements.
		ShaderHandle CreateShaderGLSL(const char* glsl_code, ShaderStage stage, uint32_t include_directory_count, std::string* include_directories);
		// Creates a shader from glsl code. Converts glsl to spirv using glslang library.
		ShaderHandle CreateShaderGLSL(const char* glsl_code, ShaderStage stage)
		{
			return CreateShaderGLSL(glsl_code, stage, 0, nullptr);
		}

		// Creates a graphics program consting of the shaders specified in shaders
		ProgramHandle CreateGraphicsProgram(const GraphicsProgramShaders& shaders);
		// Creates a compute program consting of the shaders specified in shaders
		ProgramHandle CreateComputeProgram(const ComputeProgramShaders& shaders);

		// Map and unmap buffer objects, access indicates whether the memory will be written to, read from, or both.
		void* MapHostBuffer(const Buffer& buffer, MemoryAccessFlags access);
		// Access indicates whether the memory was written to, read from, or both scince maphostbuffer().
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
		Context* context;

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

		void RequestVertexBlock(BufferBlock& block, VkDeviceSize size);
		void RequestIndexBlock(BufferBlock& block, VkDeviceSize size);
		void RequestUniformBlock(BufferBlock& block, VkDeviceSize size);
		void RequestStagingBlock(BufferBlock& block, VkDeviceSize size);

		void SetAcquireSemaphore(unsigned index, Semaphore acquire);
		Semaphore ConsumeReleaseSemaphore();

		const Framebuffer& RequestFramebuffer(const RenderPassInfo& info);
		const RenderPass& RequestRenderPass(const RenderPassInfo& info, bool compatible);

		VkPhysicalDeviceMemoryProperties mem_props;
		VkPhysicalDeviceProperties gpu_props;

		const DeviceFeatures* ext;
		//Creates every type of stock sampler (by creating 1 sampler for each enum type)
		void InitStockSamplers();
		void InitTimelineSemaphores();
		void InitGlslang();
		void DeinitTimelineSemaphores();
		void DeinitGlslang();

		// Make sure this is deleted last.
		HandlePool handle_pool;

		DeviceManagers managers;

//#ifdef QM_VULKAN_MT
//		Quantum::ThreadGroup thread_group;
//		std::mutex thread_group_mutex;
//#endif

		DeviceLock lock;

		// Must be freed after per_frame stuff
		VulkanIntrusiveObjectPool<DescriptorSetAllocator> descriptor_set_allocators;

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

		VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
		VulkanCache<RenderPass> render_passes;

		bool InitPipelineCache(const uint8_t* initial_cache_data, size_t initial_cache_size);

		FramebufferAllocator framebuffer_allocator;
		TransientAttachmentAllocator transient_allocator;

		VulkanDynamicArrayPool array_pool;

		// Many vulkan functions take in arrays of structs as parameters. Allocating variable sized arrays for those calls is expensive.
		// The heap array pool is the solution to this. It contains a pool of already allocated arrays. When a new array is requested
		// the pool either allocates another array, finds an array big enough, or resizes a smaller array.
		template<typename T>
		Util::RetainedDynamicArray<T> AllocateHeapArray(size_t count)
		{
			return array_pool.RetainedAllocateArray<T>(count);
		}

		template<typename T>
		void FreeHeapArray(Util::RetainedDynamicArray<T> heap_array)
		{
			array_pool.RetainedFreeArray(heap_array);
		}

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
		void DestroySampler(VkSampler sampler);
		void DestroyFramebuffer(VkFramebuffer framebuffer);
		void DestroySemaphore(VkSemaphore semaphore);
		void RecycleSemaphore(VkSemaphore semaphore);
		void DestroyEvent(VkEvent event);
		void ResetFence(VkFence fence, bool observed_wait);
		void KeepHandleAlive(ImageHandle handle);

		void DestroyProgram(Program* program);
		void DestroyShader(Shader* shader);

		void DestroyBufferNolock(VkBuffer buffer, const DeviceAllocation& allocation);
		void DestroyImageNolock(VkImage image, const DeviceAllocation& allocation);
		void DestroyImageViewNolock(VkImageView view);
		void DestroyBufferViewNolock(VkBufferView view);
		void DestroySamplerNolock(VkSampler sampler);
		void DestroyFramebufferNolock(VkFramebuffer framebuffer);
		void DestroySemaphoreNolock(VkSemaphore semaphore);
		void RecycleSemaphoreNolock(VkSemaphore semaphore);
		void DestroyEventNolock(VkEvent event);
		void ResetFenceNolock(VkFence fence, bool observed_wait);

		void FlushFrameNolock();
		CommandBufferHandle RequestCommandBufferNolock(unsigned thread_index, CommandBuffer::Type type);
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

		ImplementationWorkarounds workarounds;
		void InitWorkarounds();

		void FillBufferSharingIndices(VkBufferCreateInfo& create_info, uint32_t* sharing_indices);
	};

}
