#pragma once

#include "format.hpp"

#include "quantumvk/vulkan/misc/cookie.hpp"

#include "quantumvk/vulkan/vulkan_common.hpp"
#include "quantumvk/vulkan/vulkan_headers.hpp"

#include "quantumvk/vulkan/memory/memory_allocator.hpp"

namespace Vulkan
{
	//Forward declare device
	class Device;

	//Convert Image Usage to stages the image might be used in
	static inline VkPipelineStageFlags ImageUsageToPossibleStages(VkImageUsageFlags usage)
	{
		VkPipelineStageFlags flags = 0;

		if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
			flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
			flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
			flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

		if (usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
		{
			VkPipelineStageFlags possible = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

			if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
				possible |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

			flags &= possible;
		}

		return flags;
	}

	//Convert Layout to possible memory access
	static inline VkAccessFlags ImageLayoutToPossibleAccess(VkImageLayout layout)
	{
		switch (layout)
		{
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
			return VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			return VK_ACCESS_TRANSFER_READ_BIT;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			return VK_ACCESS_TRANSFER_WRITE_BIT;
		default:
			return ~0u;
		}
	}

	// Convert Usage to stages the image may be used in
	static inline VkAccessFlags ImageUsageToPossibleAccess(VkImageUsageFlags usage)
	{
		VkAccessFlags flags = 0;

		if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			flags |= VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
			flags |= VK_ACCESS_SHADER_READ_BIT;
		if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
			flags |= VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			flags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
			flags |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

		// Transient attachments can only be attachments, and never other resources.
		if (usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
		{
			flags &= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		}

		return flags;
	}

	//Get mip levels from extent
	static inline uint32_t ImageNumMipLevels(const VkExtent3D& extent)
	{
		uint32_t size = std::max(std::max(extent.width, extent.height), extent.depth);
		uint32_t levels = 0;
		while (size)
		{
			levels++;
			size >>= 1;
		}
		return levels;
	}

	//Get format features from usage
	static inline VkFormatFeatureFlags ImageUsageToFeatures(VkImageUsageFlags usage)
	{
		VkFormatFeatureFlags flags = 0;
		if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
			flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
		if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
			flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
			flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

		return flags;
	}

	//Data used to initialize image
	struct ImageInitialData
	{
		ImageInitialData* next_mip = nullptr;
		const void* data = nullptr;
	};

	// Misc Image Create Info Flags
	// Normally image sharing mode is Exclusive and owned by the graphics queue family
	// Pipeline barriers can be used to transfer this ownership. Alternatively the concurrent
	// flags can be set to indicate what queues can own the image.
	enum ImageMiscFlagBits
	{
		// Causes lower mip levels to be automatically filled using linear blitting
		IMAGE_MISC_GENERATE_MIPS_BIT = 1 << 0,
		// Forces the default image view to be an array type, even if create_info only has one layer
		IMAGE_MISC_FORCE_ARRAY_BIT = 1 << 1,
		IMAGE_MISC_MUTABLE_SRGB_BIT = 1 << 2,
		// This flags make the CreateImage call check that linear filtering is supported. If not, a null image is returned.
		IMAGE_MISC_VERIFY_FORMAT_FEATURE_SAMPLED_LINEAR_FILTER_BIT = 1 << 7,
		IMAGE_MISC_LINEAR_IMAGE_IGNORE_DEVICE_LOCAL_BIT = 1 << 8
	};
	using ImageMiscFlags = uint32_t;

	//Misc ImageView Create Info Flags
	enum ImageViewMiscFlagBits
	{
		IMAGE_VIEW_MISC_FORCE_ARRAY_BIT = 1 << 0
	};
	using ImageViewMiscFlags = uint32_t;

	//Forward declare image
	class Image;

	//Specifies how to create an image view
	struct ImageViewCreateInfo
	{
		Image* image = nullptr;
		VkFormat format = VK_FORMAT_UNDEFINED;
		unsigned base_level = 0;
		unsigned levels = VK_REMAINING_MIP_LEVELS;
		unsigned base_layer = 0;
		unsigned layers = VK_REMAINING_ARRAY_LAYERS;
		VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
		ImageViewMiscFlags misc = 0;
		VkComponentMapping swizzle = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A,};
	};

	//Forward Declare image view
	class ImageView;

	//Functor to delete image view
	struct ImageViewDeleter
	{
		void operator()(ImageView* view);
	};

	//Ref-counted vkImageView wrapper
	class ImageView : public Util::IntrusivePtrEnabled<ImageView, ImageViewDeleter, HandleCounter>, public Cookie, public InternalSyncEnabled
	{
	public:
		friend struct ImageViewDeleter;

		ImageView(Device* device, VkImageView view, const ImageViewCreateInfo& info);

		~ImageView();

		void SetAltViews(VkImageView depth, VkImageView stencil)
		{
			VK_ASSERT(depth_view == VK_NULL_HANDLE);
			VK_ASSERT(stencil_view == VK_NULL_HANDLE);
			depth_view = depth;
			stencil_view = stencil;
		}

		void SetRenderTargetViews(std::vector<VkImageView> views)
		{
			VK_ASSERT(render_target_views.empty());
			render_target_views = std::move(views);
		}

		void SetUnormView(VkImageView view_)
		{
			VK_ASSERT(unorm_view == VK_NULL_HANDLE);
			unorm_view = view_;
		}

		void SetSrgbView(VkImageView view_)
		{
			VK_ASSERT(srgb_view == VK_NULL_HANDLE);
			srgb_view = view_;
		}

		// By default, gets a combined view which includes all aspects in the image.
		// This would be used mostly for render targets.
		VkImageView GetView() const
		{
			return view;
		}

		VkImageView GetRenderTargetView(unsigned layer) const;

		// Gets an image view which only includes floating point domains.
		// Takes effect when we want to sample from an image which is Depth/Stencil,
		// but we only want to sample depth.
		VkImageView GetFloatView() const
		{
			return depth_view != VK_NULL_HANDLE ? depth_view : view;
		}

		// Gets an image view which only includes integer domains.
		// Takes effect when we want to sample from an image which is Depth/Stencil,
		// but we only want to sample stencil.
		VkImageView GetIntegerView() const
		{
			return stencil_view != VK_NULL_HANDLE ? stencil_view : view;
		}

		VkImageView GetUnormView() const
		{
			return unorm_view;
		}

		VkImageView GetSRGBView() const
		{
			return srgb_view;
		}

		VkFormat GetFormat() const
		{
			return info.format;
		}

		const Image& GetImage() const
		{
			return *info.image;
		}

		Image& GetImage()
		{
			return *info.image;
		}

		const ImageViewCreateInfo& GetCreateInfo() const
		{
			return info;
		}

	private:
		Device* device;
		VkImageView view;
		std::vector<VkImageView> render_target_views;
		VkImageView depth_view = VK_NULL_HANDLE;
		VkImageView stencil_view = VK_NULL_HANDLE;
		VkImageView unorm_view = VK_NULL_HANDLE;
		VkImageView srgb_view = VK_NULL_HANDLE;
		ImageViewCreateInfo info;
	};

	using ImageViewHandle = Util::IntrusivePtr<ImageView>;

	//Memory type of image
	enum class ImageDomain
	{
		Physical, // Device local
		Transient, // Not backed by real memory, used for transient attachments
		LinearHostCached, // Visible on host as linear stream of pixels (preferes to be cached)
		LinearHost // Visible on host as linear stream of pixels (preferes to be coherent)
	};

	//Specifies image view creation
	struct ImageCreateInfo
	{
		ImageDomain domain = ImageDomain::Physical;
		unsigned width = 0;
		unsigned height = 0;
		unsigned depth = 1;
		unsigned levels = 1;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImageType type = VK_IMAGE_TYPE_2D;
		unsigned layers = 1;
		VkImageUsageFlags usage = 0;
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
		VkImageCreateFlags flags = 0;
		ImageMiscFlags misc = 0;
		VkImageLayout initial_layout = VK_IMAGE_LAYOUT_GENERAL;
		VkComponentMapping swizzle = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A, };

		static ImageCreateInfo Immutable2dImage(unsigned width, unsigned height, VkFormat format, bool mipmapped = false)
		{
			ImageCreateInfo info;
			info.width = width;
			info.height = height;
			info.depth = 1;
			info.levels = mipmapped ? 0u : 1u;
			info.format = format;
			info.type = VK_IMAGE_TYPE_2D;
			info.layers = 1;
			info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.flags = 0;
			info.misc = mipmapped ? unsigned(IMAGE_MISC_GENERATE_MIPS_BIT) : 0u;
			info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			return info;
		}

		static ImageCreateInfo
			Immutable3dImage(unsigned width, unsigned height, unsigned depth, VkFormat format, bool mipmapped = false)
		{
			ImageCreateInfo info = Immutable2dImage(width, height, format, mipmapped);
			info.depth = depth;
			info.type = VK_IMAGE_TYPE_3D;
			return info;
		}

		static ImageCreateInfo RenderTarget(unsigned width, unsigned height, VkFormat format)
		{
			ImageCreateInfo info;
			info.width = width;
			info.height = height;
			info.depth = 1;
			info.levels = 1;
			info.format = format;
			info.type = VK_IMAGE_TYPE_2D;
			info.layers = 1;
			info.usage = (format_has_depth_or_stencil_aspect(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.flags = 0;
			info.misc = 0;
			info.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
			return info;
		}

		static ImageCreateInfo TransientRenderTarget(unsigned width, unsigned height, VkFormat format)
		{
			ImageCreateInfo info;
			info.domain = ImageDomain::Transient;
			info.width = width;
			info.height = height;
			info.depth = 1;
			info.levels = 1;
			info.format = format;
			info.type = VK_IMAGE_TYPE_2D;
			info.layers = 1;
			info.usage = (format_has_depth_or_stencil_aspect(format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) |
				VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.flags = 0;
			info.misc = 0;
			info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			return info;
		}

		static uint32_t ComputeViewFormats(const ImageCreateInfo& info, VkFormat* formats)
		{
			if ((info.misc & IMAGE_MISC_MUTABLE_SRGB_BIT) == 0)
				return 0;

			switch (info.format)
			{
			case VK_FORMAT_R8G8B8A8_UNORM:
			case VK_FORMAT_R8G8B8A8_SRGB:
				formats[0] = VK_FORMAT_R8G8B8A8_UNORM;
				formats[1] = VK_FORMAT_R8G8B8A8_SRGB;
				return 2;

			case VK_FORMAT_B8G8R8A8_UNORM:
			case VK_FORMAT_B8G8R8A8_SRGB:
				formats[0] = VK_FORMAT_B8G8R8A8_UNORM;
				formats[1] = VK_FORMAT_B8G8R8A8_SRGB;
				return 2;

			case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
			case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
				formats[0] = VK_FORMAT_A8B8G8R8_UNORM_PACK32;
				formats[1] = VK_FORMAT_A8B8G8R8_SRGB_PACK32;
				return 2;

			default:
				return 0;
			}
		}
	};

	//Specifies layout type
	enum class Layout
	{
		Optimal,
		General
	};

	class Image;

	struct ImageDeleter
	{
		void operator()(Image* image);
	};

	//Ref counted vkImage and vmaAllocation wrapper
	class Image : public Util::IntrusivePtrEnabled<Image, ImageDeleter, HandleCounter>, public Cookie, public InternalSyncEnabled
	{
	public:
		friend struct ImageDeleter;

		~Image();

		Image(Image&&) = delete;

		Image& operator=(Image&&) = delete;

		const ImageView& GetView() const
		{
			VK_ASSERT(view);
			return *view;
		}

		ImageView& GetView()
		{
			VK_ASSERT(view);
			return *view;
		}

		VkImage GetImage() const
		{
			return image;
		}

		VkFormat GetFormat() const
		{
			return create_info.format;
		}

		uint32_t GetWidth(uint32_t lod = 0) const
		{
			return std::max(1u, create_info.width >> lod);
		}

		uint32_t GetHeight(uint32_t lod = 0) const
		{
			return std::max(1u, create_info.height >> lod);
		}

		uint32_t GetDepth(uint32_t lod = 0) const
		{
			return std::max(1u, create_info.depth >> lod);
		}

		const ImageCreateInfo& GetCreateInfo() const
		{
			return create_info;
		}

		VkImageLayout GetLayout(VkImageLayout optimal) const
		{
			return layout_type == Layout::Optimal ? optimal : VK_IMAGE_LAYOUT_GENERAL;
		}

		Layout GetLayoutType() const
		{
			return layout_type;
		}

		void SetLayout(Layout layout)
		{
			layout_type = layout;
		}

		bool IsSwapchainImage() const
		{
			return swapchain_layout != VK_IMAGE_LAYOUT_UNDEFINED;
		}

		VkImageLayout GetSwapchainLayout() const
		{
			return swapchain_layout;
		}

		void SetSwapchainLayout(VkImageLayout layout)
		{
			swapchain_layout = layout;
		}

		void SetStageFlags(VkPipelineStageFlags flags)
		{
			stage_flags = flags;
		}

		void SetAccessFlags(VkAccessFlags flags)
		{
			access_flags = flags;
		}

		VkPipelineStageFlags GetStageFlags() const
		{
			return stage_flags;
		}

		VkAccessFlags GetAccessFlags() const
		{
			return access_flags;
		}

		const DeviceAllocation& GetAllocation() const
		{
			return alloc;
		}

		void DisownImage();

	private:
		friend class Util::ObjectPool<Image>;

		Image(Device* device, VkImage image, VkImageView default_view, const DeviceAllocation& alloc, const ImageCreateInfo& info, VkImageViewType view_type);

		Device* device;
		VkImage image;
		ImageViewHandle view;
		DeviceAllocation alloc;
		ImageCreateInfo create_info;

		Layout layout_type = Layout::Optimal;
		VkPipelineStageFlags stage_flags = 0;
		VkAccessFlags access_flags = 0;
		VkImageLayout swapchain_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		bool owns_image = true;
		bool owns_memory_allocation = true;
	};

	using ImageHandle = Util::IntrusivePtr<Image>;

	//Forward declare linear host image
	class LinearHostImage;
	//Functor to delete linear host image
	struct LinearHostImageDeleter
	{
		void operator()(LinearHostImage* image);
	};
	//Forward declare buffer
	class Buffer;
	//Linear Host image create info flags
	enum LinearHostImageCreateInfoFlagBits
	{
		LINEAR_HOST_IMAGE_HOST_CACHED_BIT = 1 << 0,
		LINEAR_HOST_IMAGE_REQUIRE_LINEAR_FILTER_BIT = 1 << 1,
		LINEAR_HOST_IMAGE_IGNORE_DEVICE_LOCAL_BIT = 1 << 2
	};
	using LinearHostImageCreateInfoFlags = uint32_t;

	//Linear host image create info
	struct LinearHostImageCreateInfo
	{
		unsigned width = 0;
		unsigned height = 0;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImageUsageFlags usage = 0;
		VkPipelineStageFlags stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		LinearHostImageCreateInfoFlags flags = 0;
	};

	// Special image type which supports direct CPU mapping.
	// Useful optimization for UMA implementations of Vulkan where we don't necessarily need
	// to perform staging copies. It gracefully falls back to staging buffer as needed.
	// Only usage flag SAMPLED_BIT is currently supported.
	class LinearHostImage : public Util::IntrusivePtrEnabled<LinearHostImage, LinearHostImageDeleter, HandleCounter>
	{
	public:
		friend struct LinearHostImageDeleter;

		size_t GetRowPitchBytes() const;
		size_t GetOffset() const;
		const ImageView& GetView() const;
		const Image& GetImage() const;
		const DeviceAllocation& GetHostVisibleAllocation() const;
		const Buffer& GetHostVisibleBuffer() const;
		bool NeedStagingCopy() const;
		VkPipelineStageFlags GetUsedPipelineStages() const;

	private:
		friend class Util::ObjectPool<LinearHostImage>;
		LinearHostImage(Device* device, ImageHandle gpu_image, Util::IntrusivePtr<Buffer> cpu_image, VkPipelineStageFlags stages);
		Device* device;
		ImageHandle gpu_image;
		Util::IntrusivePtr<Buffer> cpu_image;
		VkPipelineStageFlags stages;
		size_t row_pitch;
		size_t row_offset;
	};
	using LinearHostImageHandle = Util::IntrusivePtr<LinearHostImage>;

}