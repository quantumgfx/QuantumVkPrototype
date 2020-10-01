#include "image.hpp"
#include "quantumvk/vulkan/device.hpp"
#include "quantumvk/vulkan/memory/buffer.hpp"

using namespace std;

namespace Vulkan
{

	ImageView::ImageView(Device* device_, VkImageView view_, const ImageViewCreateInfo& info_)
		: Cookie(device_)
		, device(device_)
		, view(view_)
		, info(info_)
	{
	}

	VkImageView ImageView::GetRenderTargetView(unsigned layer) const
	{
		// Transient images just have one layer.
		if (info.image->GetCreateInfo().domain == ImageDomain::Transient)
			return view;

		VK_ASSERT(layer < GetCreateInfo().layers);

		if (render_target_views.empty())
			return view;
		else
		{
			VK_ASSERT(layer < render_target_views.size());
			return render_target_views[layer];
		}
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
			if (unorm_view != VK_NULL_HANDLE)
				device->DestroyImageViewNolock(unorm_view);
			if (srgb_view != VK_NULL_HANDLE)
				device->DestroyImageViewNolock(srgb_view);

			for (auto& v : render_target_views)
				device->DestroyImageViewNolock(v);
		}
		else
		{
			device->DestroyImageView(view);
			if (depth_view != VK_NULL_HANDLE)
				device->DestroyImageView(depth_view);
			if (stencil_view != VK_NULL_HANDLE)
				device->DestroyImageView(stencil_view);
			if (unorm_view != VK_NULL_HANDLE)
				device->DestroyImageView(unorm_view);
			if (srgb_view != VK_NULL_HANDLE)
				device->DestroyImageView(srgb_view);

			for (auto& v : render_target_views)
				device->DestroyImageView(v);
		}
	}

	Image::Image(Device* device_, VkImage image_, VkImageView default_view, const DeviceAllocation& alloc_,
		const ImageCreateInfo& create_info_, VkImageViewType view_type)
		: Cookie(device_)
		, device(device_)
		, image(image_)
		, alloc(alloc_)
		, create_info(create_info_)
	{
		if (default_view != VK_NULL_HANDLE)
		{
			ImageViewCreateInfo info;
			info.image = this;
			info.view_type = view_type;
			info.format = create_info.format;
			info.base_level = 0;
			info.levels = create_info.levels;
			info.base_layer = 0;
			info.layers = create_info.layers;
			view = ImageViewHandle(device->handle_pool.image_views.allocate(device, default_view, info));
		}
	}

	void Image::DisownImage()
	{
		owns_image = false;
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

	const ImageView& LinearHostImage::GetView() const
	{
		return gpu_image->GetView();
	}

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
			row_pitch = gpu_image->GetWidth() * TextureFormatLayout::format_block_size(gpu_image->GetFormat(), FormatToAspectMask(gpu_image->GetFormat()));
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
