#pragma once

#include "../base/vk.hpp"
#include "memory.hpp"

namespace vkq
{
    class BufferView
    {
    public:
        BufferView() = default;
        ~BufferView() = default;

    public:
        static BufferView create(const Buffer& buffer, vk::Format format, vk::DeviceSize offset, vk::DeviceSize range, vk::BufferViewCreateFlags flags = {}, VkNextProxy<vk::BufferViewCreateInfo> next = nullptr);
        static BufferView create(const Buffer& buffer, const vk::BufferViewCreateInfo& createInfo);

        void destroy();

        Buffer buffer() const { return buffer_; }

        vk::BufferView vkBufferView() const { return view_; }
        vk::BufferView vkHandle() const { return view_; }
        operator vk::BufferView() const { return view_; }

    private:
        explicit BufferView(Buffer buffer, vk::BufferView view);

        Buffer buffer_;
        vk::BufferView view_;
    };

    class ImageView
    {
    public:
        ImageView() = default;
        ~ImageView() = default;

    public:
        static ImageView create(const Image& image, vk::ImageViewType viewType, vk::Format format, vk::ImageSubresourceRange subresourceRange, VkComponentMapping components, vk::ImageViewCreateFlags flags, VkNextProxy<vk::ImageViewCreateInfo> next = nullptr);

        void destroy();

        Image image() const { return image_; }

        vk::ImageView vkImageView() const { return view_; }
        vk::ImageView vkHandle() const { return view_; }
        operator vk::ImageView() const { return view_; }

    private:
        explicit ImageView(Image image, vk::ImageView view);

        Image image_;
        vk::ImageView view_;
    };
}