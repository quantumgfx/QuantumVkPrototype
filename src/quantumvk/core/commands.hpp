#pragma once

#include "device.hpp"
#include "queue.hpp"

namespace vkq
{
    class CommandPool
    {
    public:
        CommandPool() = default;
        ~CommandPool() = default;

    public:
        static CommandPool create(const Device& device, const vk::CommandPoolCreateInfo& createInfo);

        static CommandPool create(const QueueFamily& family, vk::CommandPoolCreateFlags flags = {}, const void* pNext = nullptr)
        {
            vk::CommandPoolCreateInfo createInfo{};
            createInfo.pNext = pNext;
            createInfo.flags = flags;
            createInfo.queueFamilyIndex = family.getIndex();

            return create(family.getDevice(), createInfo);
        }

        void destory();

#ifdef VK_VERSION_1_1

        void trim(vk::CommandPoolTrimFlags flags)
        {
            device.trimCommandPool(commandPool, flags);
        }

#endif

#ifdef VK_KHR_MAINTENANCE1_EXTENSION_NAME

        void trimKHR(vk::CommandPoolTrimFlags flags)
        {
            device.trimCommandPoolKHR(commandPool, flags);
        }

#endif

        Device getDevice() const
        {
            return device;
        }

        vk::CommandPool vkCommandPool() const { return commandPool; }
        vk::CommandPool vkHandle() const { return commandPool; }
        operator vk::CommandPool() const { return commandPool; }

    private:
        Device device;
        vk::CommandPool commandPool;
    };
} // namespace vkq
