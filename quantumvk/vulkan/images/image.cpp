#include "image.hpp"
#include "quantumvk/vulkan/device.hpp"
#include "quantumvk/vulkan/memory/buffer.hpp"

using namespace std;

namespace Vulkan
{

	ImageView::ImageView(Device* device_, VkImageView view_, VkImageView depth_, VkImageView stencil_, const ImageViewCreateInfo& info_)
		: Cookie(device_)
		, device(device_)
		, view(view_)
		, depth_view(depth_)
		, stencil_view(stencil_)
		, info(info_)
	{
	}

	ImageView::~ImageView()
	{
		if (internal_sync)
		{
			device->DestroyImageViewNolock(view);
			if (depth_view != VK_NULL_HANDLE)
				device->DestroyImageViewNolock(depth_view);
			if (stencil_view != VK_NULL_HANDLE)
				device->DestroyImageViewNolock(stencil_view);
		}
		else
		{
			device->DestroyImageView(view);
			if (depth_view != VK_NULL_HANDLE)
				device->DestroyImageView(depth_view);
			if (stencil_view != VK_NULL_HANDLE)
				device->DestroyImageView(stencil_view);
		}
	}

	Image::Image(Device* device_, VkImage image_, const DeviceAllocation& alloc_, const ImageCreateInfo& create_info_)
		: Cookie(device_)
		, device(device_)
		, image(image_)
		, alloc(alloc_)
		, create_info(create_info_)
	{

		custom_view_formats.reserve(create_info_.num_custom_view_formats);
		for (uint32_t i = 0; i < create_info_.num_custom_view_formats; i++)
			custom_view_formats.push_back(create_info_.custom_view_formats[i]);

		if (create_info.num_custom_view_formats > 0)
			create_info.custom_view_formats = custom_view_formats.data();
		else
			create_info.custom_view_formats = nullptr;
	}

	void Image::DisownImage()
	{
		owns_image = false;
	}

	bool Image::ImageViewFormatSupported(VkFormat view_format) const
	{
		if (create_info.view_formats == ImageViewFormats::Same)
			return create_info.format == view_format;
		
		if (create_info.view_formats == ImageViewFormats::Custom)
		{
			for (const VkFormat& format : custom_view_formats)
			{
				if (format == view_format)
					return true;
			}

			return false;
		}
		
		return false;
	}

	Image::~Image()
	{
		if (owns_image)
		{
			if (internal_sync)
				device->DestroyImageNolock(image, alloc);
			else
				device->DestroyImage(image, alloc);
		}
	}

	const Buffer& LinearHostImage::GetHostVisibleBuffer() const
	{
		return *cpu_image;
	}

	bool LinearHostImage::NeedStagingCopy() const
	{
		return gpu_image->GetCreateInfo().domain != ImageDomain::LinearHostCached && gpu_image->GetCreateInfo().domain != ImageDomain::LinearHost;
	}

	const DeviceAllocation& LinearHostImage::GetHostVisibleAllocation() const
	{
		return NeedStagingCopy() ? cpu_image->GetAllocation() : gpu_image->GetAllocation();
	}

	/*const ImageView& LinearHostImage::GetView() const
	{
		return gpu_image->GetView();
	}*/

	const Image& LinearHostImage::GetImage() const
	{
		return *gpu_image;
	}

	size_t LinearHostImage::GetOffset() const
	{
		return row_offset;
	}

	size_t LinearHostImage::GetRowPitchBytes() const
	{
		return row_pitch;
	}

	VkPipelineStageFlags LinearHostImage::GetUsedPipelineStages() const
	{
		return stages;
	}

	LinearHostImage::LinearHostImage(Device* device_, ImageHandle gpu_image_, BufferHandle cpu_image_, VkPipelineStageFlags stages_)
		: device(device_), gpu_image(move(gpu_image_)), cpu_image(move(cpu_image_)), stages(stages_)
	{
		if (gpu_image->GetCreateInfo().domain == ImageDomain::LinearHostCached || gpu_image->GetCreateInfo().domain == ImageDomain::LinearHost)
		{
			VkImageSubresource sub = {};
			sub.aspectMask = FormatToAspectMask(gpu_image->GetFormat());
			VkSubresourceLayout layout;

			auto& table = device_->GetDeviceTable();
			table.vkGetImageSubresourceLayout(device->GetDevice(), gpu_image->GetImage(), &sub, &layout);
			row_pitch = layout.rowPitch;
			row_offset = layout.offset;
		}
		else
		{
			row_pitch = gpu_image->GetWidth() * TextureFormatLayout::FormatBlockSize(gpu_image->GetFormat(), FormatToAspectMask(gpu_image->GetFormat()));
			row_offset = 0;
		}
	}

	void ImageViewDeleter::operator()(ImageView* view)
	{
		view->device->handle_pool.image_views.free(view);
	}

	void ImageDeleter::operator()(Image* image)
	{
		image->device->handle_pool.images.free(image);
	}

	void LinearHostImageDeleter::operator()(LinearHostImage* image)
	{
		image->device->handle_pool.linear_images.free(image);
	}
}
