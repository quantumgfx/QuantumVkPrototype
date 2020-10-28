#include "device.hpp"
#include "images/format.hpp"

#include <algorithm>
#include <string.h>
#include <stdlib.h>

#ifdef QM_VULKAN_MT
#define LOCK() std::lock_guard<std::mutex> holder__{lock.lock}
#else
#define LOCK() ((void)0)
#endif

using namespace Util;

namespace Vulkan
{
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
			buffer.size = info.width * info.height * TextureFormatLayout::FormatBlockSize(info.format, FormatToAspectMask(info.format));
			cpu_image = CreateBuffer(buffer);
			if (!cpu_image)
				return LinearHostImageHandle(nullptr);
		}
		else
			gpu_image->SetLayout(Layout::General);

		return LinearHostImageHandle(handle_pool.linear_images.allocate(this, std::move(gpu_image), std::move(cpu_image), info.stages));
	}

	////////////////////////////
	//Buffer Creation///////////
	////////////////////////////

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

		if (!managers.memory.AllocateBuffer(info, alloc_info, &buffer, &allocation))
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

					uint32_t sem_count = uint32_t(compute_sem_needed) + uint32_t(graphics_sem_needed);

					Semaphore sem[2];
					Submit(cmd, nullptr, sem_count, sem);

					uint32_t sem_index = 0;
					if (compute_sem_needed)
						AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[sem_index++], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
					if (graphics_sem_needed)
						AddWaitSemaphore(CommandBuffer::Type::Generic, sem[sem_index++], possible_buffer_stages, true);

					if (sem_index == 0)
						FlushFrame(CommandBuffer::Type::AsyncTransfer);
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

					uint32_t sem_count = uint32_t(compute_sem_needed) + uint32_t(graphics_sem_needed);

					Semaphore sem[2];
					Submit(cmd, nullptr, sem_count, sem);

					uint32_t sem_index = 0;
					if (compute_sem_needed)
						AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[sem_index++], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
					if (graphics_sem_needed)
						AddWaitSemaphore(CommandBuffer::Type::Generic, sem[sem_index++], possible_buffer_stages, true);

					if (sem_index == 0)
						FlushFrame(CommandBuffer::Type::AsyncTransfer);
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

	static inline VkImageViewType GetImageViewType(const ImageCreateInfo& create_info)
	{
		unsigned layers = create_info.layers;
		unsigned base_layer = 0;

		if (layers == VK_REMAINING_ARRAY_LAYERS)
			layers = create_info.layers - base_layer;

		bool force_array = (create_info.misc & IMAGE_MISC_FORCE_ARRAY_BIT);

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
		std::vector<VkImageView> rt_views;

		VkImageViewType default_view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
		DeviceAllocator* allocator = nullptr;
		bool owned = true;

		VkImageViewType GetDefaltViewType() const
		{
			return default_view_type;
		}

		bool CreateDefaultViews(const ImageCreateInfo& create_info, const VkImageViewCreateInfo* view_info)
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
				default_view_info.viewType = GetImageViewType(create_info);
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

			return true;
		}

	private:

		bool CreateDefaultView(const VkImageViewCreateInfo& info)
		{
			VkDevice vkdevice = device->GetDevice();

			// Create the normal image view. This one contains every subresource.
			if (table.vkCreateImageView(vkdevice, &info, nullptr, &image_view) != VK_SUCCESS)
				return false;

			return true;
		}

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


		void CleanUp()
		{
			VkDevice vkdevice = device->GetDevice();

			if (image_view)
				table.vkDestroyImageView(vkdevice, image_view, nullptr);

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

		VK_ASSERT(create_info.view_type != VK_IMAGE_VIEW_TYPE_MAX_ENUM);

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
			ret->SetRenderTargetViews(std::move(holder.rt_views));
			return ret;
		}
		else
			return ImageViewHandle(nullptr);
	}

	static inline uint32_t RoundUpToNearestMultiple(uint32_t number, uint32_t multiple)
	{
		return ((number + multiple - 1) / multiple) * multiple;
	}

	static inline uint32_t AlignNumberToPowerOf2(uint32_t number, uint32_t alignment)
	{
		return (number + alignment - 1) & ~(alignment - 1);
	}

	static inline uint32_t GetRequiredSize(const ImageCreateInfo& info,  uint32_t levels)
	{
		uint32_t required_size = 0;

		// Number of bytes each block takes up
		uint32_t pixel_stride = TextureFormatLayout::FormatBlockSize(info.format, 0);

		uint32_t mip_width = info.width;
		uint32_t mip_height = info.height;
		uint32_t mip_depth = info.depth;

		for (uint32_t level = 0; level < levels; level++)
		{
			required_size = AlignNumberToPowerOf2(required_size, 16);

			required_size += info.layers * pixel_stride * mip_width * mip_height * mip_depth;

			mip_width = std::max((mip_width >> 1u), 1u);
			mip_height = std::max((mip_height >> 1u), 1u);
			mip_depth = std::max((mip_depth >> 1u), 1u);
		}

		return required_size;
	}

	InitialImageBuffer Device::CreateUncompressedImageStagingBuffer(const ImageCreateInfo& info, InitialImageData initial)
	{

#ifdef VULKAN_DEBUG
		// Dimensions of block (typically (1, 1))
		uint32_t block_dim_x, block_dim_y;
		TextureFormatLayout::FormatBlockDim(info.format, block_dim_x, block_dim_y);

		// Currently this function can only run with images with uncompressed
		VK_ASSERT(block_dim_x == 1 && block_dim_y == 1);
#endif

		bool generate_mips = (info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;

		uint32_t copy_levels;
		if (generate_mips)
			copy_levels = 1;
		else if (info.levels == 0)
			copy_levels = TextureFormatLayout::NumMiplevels(info.width, info.height, info.depth);
		else
			copy_levels = info.levels;

		uint32_t required_size = GetRequiredSize(info, copy_levels);

		uint8_t* dst = static_cast<uint8_t*>(malloc(required_size));

		uint32_t offset = 0;

		uint32_t mip_width = info.width;
		uint32_t mip_height = info.height;
		uint32_t mip_depth = info.depth;

		// Number of bytes each block takes up
		uint32_t pixel_stride = TextureFormatLayout::FormatBlockSize(info.format, 0);

		std::vector<ImageStagingCopyInfo> copies(copy_levels);

		for (unsigned level = 0; level < copy_levels; level++)
		{
			offset = AlignNumberToPowerOf2(offset, 16);

			// Data to be loaded into this level
			InitialImageLevelData& level_data = initial.levels[level];
			//const auto& mip_info = layout.GetMipInfo(level);

			// Number of bytes in a single layer
			size_t layer_stride = mip_width * mip_height * mip_depth * pixel_stride;

			auto& copy = copies[level];
			copy.buffer_offset = offset;
			copy.buffer_row_length = 0;
			copy.buffer_image_height = 0;
			copy.base_array_layer = 0;
			copy.num_layers = info.layers;
			copy.mip_level = level;
			copy.image_offset = { 0, 0, 0 };
			copy.image_extent.width = mip_width;
			copy.image_extent.height = mip_height;
			copy.image_extent.depth = mip_depth;

			for (unsigned layer = 0; layer < info.layers; layer++)
			{
				InitialImageLayerData& layer_data = level_data.layers[layer];
				if (layer_data.data)
					memcpy(dst + offset, layer_data.data, layer_stride);
				else
					memset(dst + offset, 0, layer_stride);
				offset += layer_stride;
			}

			mip_width = std::max((mip_width >> 1u), 1u);
			mip_height = std::max((mip_height >> 1u), 1u);
			mip_depth = std::max((mip_depth >> 1u), 1u);
		}

		InitialImageBuffer result = CreateImageStagingBuffer(info, required_size, dst, copies.size(), copies.data());

		free(dst);

		return result;

	}

	InitialImageBuffer Device::CreateImageStagingBuffer(const ImageCreateInfo& info, size_t buffer_size, void* buffer, uint32_t num_copies, ImageStagingCopyInfo* copies)
	{
		InitialImageBuffer result;

		BufferCreateInfo buffer_info = {};
		buffer_info.domain = BufferDomain::Host;
		buffer_info.size = buffer_size;
		buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		result.buffer = CreateBuffer(buffer_info);

		// And now, do the actual copy.
		void* mapped = MapHostBuffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT);
		memcpy(mapped, buffer, buffer_size);
		UnmapHostBuffer(*result.buffer, MEMORY_ACCESS_WRITE_BIT);

		result.blits.resize(num_copies);

		for (uint32_t i = 0; i < num_copies; i++)
		{
			const auto& copy = copies[i];

			auto& blit = result.blits[i];
			blit = {};
			blit.bufferOffset = copy.buffer_offset;
			blit.bufferRowLength = copy.buffer_row_length;
			blit.bufferImageHeight = copy.buffer_image_height;
			blit.imageSubresource.aspectMask = FormatToAspectMask(info.format);
			blit.imageSubresource.mipLevel = copy.mip_level;
			blit.imageSubresource.baseArrayLayer = copy.base_array_layer;
			blit.imageSubresource.layerCount = copy.num_layers;
			blit.imageOffset = copy.image_offset;
			blit.imageExtent = copy.image_extent;
		}

		return result;
	}

	ImageHandle Device::CreateImage(const ImageCreateInfo& create_info, ResourceQueueOwnershipFlags ownership)
	{
		return CreateImageFromStagingBuffer(create_info, ownership, nullptr);
	}

	ImageHandle Device::CreateImage(const ImageCreateInfo& info, ResourceQueueOwnershipFlags ownership, size_t buffer_size, void* buffer, uint32_t num_copies, ImageStagingCopyInfo* copies)
	{
		if (buffer)
		{
			auto staging_buffer = CreateImageStagingBuffer(info, buffer_size, buffer, num_copies, copies);
			return CreateImageFromStagingBuffer(info, ownership, &staging_buffer);
		}
		else
			return CreateImageFromStagingBuffer(info, ownership, nullptr);
	}

	ImageHandle Device::CreateUncompressedImage(const ImageCreateInfo& info, ResourceQueueOwnershipFlags ownership, InitialImageData initial)
	{
		if (initial.levels)
		{
			auto staging_buffer = CreateUncompressedImageStagingBuffer(info, initial);
			return CreateImageFromStagingBuffer(info, ownership, &staging_buffer);
		}
		else
			return CreateImageFromStagingBuffer(info, ownership, nullptr);
	}

	ImageHandle Device::CreateImageFromStagingBuffer(const ImageCreateInfo& create_info, ResourceQueueOwnershipFlags ownership, const InitialImageBuffer* staging_buffer)
	{
		VK_ASSERT(ownership);

		bool is_exclusive = ownership & (RESOURCE_EXCLUSIVE_GENERIC | RESOURCE_EXCLUSIVE_ASYNC_COMPUTE | RESOURCE_EXCLUSIVE_ASYNC_GRAPHICS | RESOURCE_EXCLUSIVE_ASYNC_TRANSFER);
		bool is_concurrent = ownership & (RESOURCE_CONCURRENT_GENERIC | RESOURCE_CONCURRENT_ASYNC_COMPUTE | RESOURCE_CONCURRENT_ASYNC_GRAPHICS | RESOURCE_CONCURRENT_ASYNC_TRANSFER);
		bool generate_mips = (create_info.misc & IMAGE_MISC_GENERATE_MIPS_BIT) != 0;

		VK_ASSERT(!(is_exclusive && is_concurrent));

		bool is_async_graphics_on_compute_queue = GetPhysicalQueueType(CommandBuffer::Type::AsyncGraphics) == CommandBuffer::Type::AsyncCompute;

		bool is_concurrent_graphics = (ownership & RESOURCE_CONCURRENT_GENERIC) || (!is_async_graphics_on_compute_queue && (ownership & RESOURCE_CONCURRENT_ASYNC_GRAPHICS));
		bool is_concurrent_compute = (ownership & RESOURCE_CONCURRENT_ASYNC_COMPUTE) || (is_async_graphics_on_compute_queue && (ownership & RESOURCE_CONCURRENT_ASYNC_GRAPHICS));
		bool is_concurrent_transfer = ownership & RESOURCE_CONCURRENT_ASYNC_TRANSFER;

		uint32_t exclusive_target_queue_index = 0;
		CommandBuffer::Type exclusive_owner;
		if (ownership & RESOURCE_EXCLUSIVE_GENERIC)
		{
			exclusive_target_queue_index = graphics_queue_family_index;
			exclusive_owner = CommandBuffer::Type::Generic;
		}
		else if (ownership & RESOURCE_EXCLUSIVE_ASYNC_GRAPHICS)
		{
			exclusive_target_queue_index = is_async_graphics_on_compute_queue ? compute_queue_family_index : graphics_queue_family_index;
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

			if (generate_mips || (ownership & RESOURCE_CONCURRENT_GENERIC) != 0)
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

		auto tmpinfo = create_info;
		tmpinfo.usage = info.usage;
		tmpinfo.flags = info.flags;
		tmpinfo.levels = info.mipLevels;

		bool has_view = (info.usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) != 0;

		VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
		if (has_view)
		{
			if (!holder.CreateDefaultViews(tmpinfo, nullptr))
				return ImageHandle(nullptr);
			view_type = holder.GetDefaltViewType();
		}

		ImageHandle handle(handle_pool.images.allocate(this, holder.image, holder.image_view, holder.allocation, tmpinfo, view_type));
		if (handle)
		{
			holder.owned = false;
			handle->GetView().SetAltViews(holder.depth_view, holder.stencil_view);
			handle->GetView().SetRenderTargetViews(std::move(holder.rt_views));
			// Set possible dstStage and dstAccess.
			handle->SetStageFlags(ImageUsageToPossibleStages(info.usage));
			handle->SetAccessFlags(ImageUsageToPossibleAccess(info.usage));
		}

		// Copy initial data to texture.
		if (staging_buffer)
		{
			VK_ASSERT(create_info.domain != ImageDomain::Transient);
			VK_ASSERT(create_info.initial_layout != VK_IMAGE_LAYOUT_UNDEFINED);

			// Now we've used the TRANSFER queue to copy data over to the GPU.
			// For mipmapping, we're now moving over to graphics,
			// the transfer queue is designed for CPU <-> GPU and that's it.

			// For concurrent queue mode, we just need to inject a semaphore.
			// For non-concurrent queue mode, we will have to inject ownership transfer barrier if the queue families do not match.

			VkPipelineStageFlags possible_image_stages = handle->GetStageFlags();
			VkAccessFlags possible_image_access = handle->GetAccessFlags() & ImageLayoutToPossibleAccess(create_info.initial_layout);

			if (is_concurrent)
			{ // If concurrent
				CommandBufferHandle transfer_cmd = RequestCommandBuffer(CommandBuffer::Type::AsyncTransfer);

				transfer_cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_ACCESS_TRANSFER_WRITE_BIT);

				transfer_cmd->CopyBufferToImage(*handle, *staging_buffer->buffer, staging_buffer->blits.size(), staging_buffer->blits.data());

				if (generate_mips)
				{ // If concurrent and generating mips

					CommandBufferHandle graphics_cmd;

					if (transfer_queue == graphics_queue)
					{
						graphics_cmd = transfer_cmd;
					}
					else
					{
						Semaphore sem;
						Submit(transfer_cmd, nullptr, 1, &sem);
						AddWaitSemaphore(CommandBuffer::Type::Generic, sem, VK_PIPELINE_STAGE_TRANSFER_BIT, true);

						graphics_cmd = RequestCommandBuffer(CommandBuffer::Type::Generic);
					}

					graphics_cmd->BarrierPrepareGenerateMipmap(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, true);
					graphics_cmd->GenerateMipmap(*handle);
					graphics_cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, create_info.initial_layout, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, possible_image_stages, possible_image_access);

					bool compute_queue_wait_on_graphics_cmd = (graphics_queue != compute_queue) && is_concurrent_compute;
					bool transfer_queue_wait_on_graphics_cmd = (graphics_queue != transfer_queue) && is_concurrent_transfer;

					uint32_t sem_count = uint32_t(compute_queue_wait_on_graphics_cmd) + uint32_t(transfer_queue_wait_on_graphics_cmd);

					Semaphore sem[2];
					Submit(graphics_cmd, nullptr, sem_count, sem);

					uint32_t sem_index = 0;
					if (compute_queue_wait_on_graphics_cmd)
						AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[sem_index++], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
					if (transfer_queue_wait_on_graphics_cmd)
						AddWaitSemaphore(CommandBuffer::Type::AsyncTransfer, sem[sem_index++], VK_PIPELINE_STAGE_TRANSFER_BIT, true);

					if (sem_index == 0)
						FlushFrame(CommandBuffer::Type::Generic);
				}
				else
				{ // If concurrent and not generating mips
					transfer_cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, create_info.initial_layout, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, possible_image_stages, possible_image_access);

					bool graphics_queue_wait_on_transfer_cmd = (transfer_queue != graphics_queue) && is_concurrent_graphics;
					bool compute_queue_wait_on_transfer_cmd = (transfer_queue != compute_queue) && is_concurrent_compute;

					uint32_t sem_count = uint32_t(graphics_queue_wait_on_transfer_cmd) + uint32_t(compute_queue_wait_on_transfer_cmd);

					Semaphore sem[2];
					Submit(transfer_cmd, nullptr, sem_count, sem);

					uint32_t sem_index = 0;
					if (graphics_queue_wait_on_transfer_cmd)
						AddWaitSemaphore(CommandBuffer::Type::Generic, sem[sem_index++], possible_image_stages, true);
					if (compute_queue_wait_on_transfer_cmd)
						AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[sem_index++], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);

					if (sem_index == 0)
						FlushFrame(CommandBuffer::Type::AsyncTransfer);
				}
			}
			else
			{ // Exclusive ownership

				if (exclusive_target_queue_index == graphics_queue_family_index)
				{ // No barrier needed between graphics and target

					if (graphics_queue == transfer_queue)
					{ // No barrier needed, everything can be done on one queue

						auto cmd = RequestCommandBuffer(CommandBuffer::Type::Generic);

						cmd->ImageBarrier(*handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

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

						VkImageMemoryBarrier release{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
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
				else if (exclusive_target_queue_index != graphics_queue_family_index)
				{ // Barrier needed between graphics and target

					if (graphics_queue == transfer_queue)
					{ // No barrier between graphics queue and transfer queue 
						auto graphics_cmd = RequestCommandBuffer(CommandBuffer::Type::Generic);

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
							VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

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

							Semaphore transfer_sem;
							Submit(transfer_cmd, nullptr, 1, &transfer_sem);
							AddWaitSemaphore(CommandBuffer::Type::Generic, transfer_sem, VK_PIPELINE_STAGE_TRANSFER_BIT, true);

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

							Semaphore graphics_sem;
							Submit(graphics_cmd, nullptr, 1, &graphics_sem);
							AddWaitSemaphore(exclusive_owner, graphics_sem, possible_image_stages, true);

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

			bool compute_queue_wait_on_graphics_cmd = (graphics_queue != compute_queue) && is_concurrent_compute;
			bool transfer_queue_wait_on_graphics_cmd = (graphics_queue != transfer_queue) && is_concurrent_transfer;

			uint32_t sem_count = uint32_t(compute_queue_wait_on_graphics_cmd) + uint32_t(transfer_queue_wait_on_graphics_cmd);

			Semaphore sem[2];
			Submit(cmd, nullptr, sem_count, sem);

			uint32_t sem_index = 0;
			if (compute_queue_wait_on_graphics_cmd)
				AddWaitSemaphore(CommandBuffer::Type::AsyncCompute, sem[sem_index++], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, true);
			if (transfer_queue_wait_on_graphics_cmd)
				AddWaitSemaphore(CommandBuffer::Type::AsyncTransfer, sem[sem_index++], VK_PIPELINE_STAGE_TRANSFER_BIT, true);

			if (sem_index == 0)
				FlushFrame(CommandBuffer::Type::Generic);

		}

		return handle;
	}

	///////////////////////////////
	//Memory Mapping///////////////
	///////////////////////////////

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

}

