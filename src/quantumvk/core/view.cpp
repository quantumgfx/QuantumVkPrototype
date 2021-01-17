#include "view.hpp"

namespace vkq
{

    explicit BufferView::BufferView(Buffer buffer, vk::BufferView view)
        : buffer_(buffer), view_(view)
    {
    }

    BufferView BufferView::create(const Buffer& buffer, vk::Format format, vk::DeviceSize offset, vk::DeviceSize range, vk::BufferViewCreateFlags flags = {}, VkNextProxy<vk::BufferViewCreateInfo> next = nullptr)
    {
        vk::BufferViewCreateInfo createInfo{};
        createInfo.pNext = next;
        createInfo.flags = flags;
        createInfo.buffer = buffer;
        createInfo.format = format;
        createInfo.offset = offset;
        createInfo.range = range;

        return BufferView{buffer, buffer.device().createBufferView(createInfo)};
    }

    BufferView BufferView::create(const Buffer& buffer, const vk::BufferViewCreateInfo& createInfo)
    {
        return BufferView{buffer, buffer.device().createBufferView(createInfo)};
    }

    void BufferView::destroy()
    {
        buffer_.device().destroyBufferView(view_);

        buffer_ = {};
        view_ = nullptr;
    }

    explicit ImageView::ImageView(Image image, vk::ImageView view)
        : image_(image), view_(view)
    {
    }

    ImageView ImageView::create(const Image& image, vk::ImageViewType viewType, vk::Format format, vk::ImageSubresourceRange subresourceRange, VkComponentMapping components, vk::ImageViewCreateFlags flags, VkNextProxy<vk::ImageViewCreateInfo> next = nullptr)
    {
        vk::ImageViewCreateInfo createInfo{};
        createInfo.pNext = next;
        createInfo.flags = flags;
        createInfo.image = image;
        createInfo.viewType = viewType;
        createInfo.format = (format == vk::Format::eUndefined) ? image.format() : format;
        createInfo.components = components;
        createInfo.subresourceRange = subresourceRange;

        return ImageView{image, image.device().createImageView(createInfo)};
    }

    void ImageView::destroy()
    {
        image_.device().destroyImageView(view_);

        image_ = {};
        view_ = nullptr;
    }

} // namespace vkq