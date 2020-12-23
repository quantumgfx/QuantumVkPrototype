#pragma once

#include "instance.hpp"
#include "physicalDevice.hpp"
#include "vk.hpp"

namespace vkq
{

    class Fence
    {
    public:
        Fence(const Fence&) = default;

        Fence(vk::Fence fence)
            : fence(fence)
        {
        }

        ~Fence() = default;

        Fence& operator=(const Fence&) = default;
        Fence& operator=(vk::Fence fence_)
        {
            fence = fence_;
            return *this;
        }

        vk::Fence vkFence() const { return fence; }
        vk::Fence vkHandle() const { return fence; }

        operator vk::Fence() const { return fence; }

    private:
        vk::Fence fence;
    };

    class Device
    {
        struct VkqType
        {
            vk::Instance instance;
            vk::PhysicalDevice physicalDevice;
            vk::Device device;
            vk::DispatchLoaderDynamic dispatch;
        };

    public:
        Device() = default;
        ~Device() = default;

    public:
        static Device create(Instance instance, vk::PhysicalDevice physicalDevice, const vk::DeviceCreateInfo& createInfo);
        static Device create(PFN_vkGetInstanceProcAddr getInstanceProcAddr, vk::Instance instance, vk::PhysicalDevice physicalDevice, const vk::DeviceCreateInfo& createInfo);

        static Device create(PhysicalDevice physicalDevice, const vk::DeviceCreateInfo& createInfo) { return create(physicalDevice.getInstance(), physicalDevice.vkPhysicalDevice(), createInfo); }

        void destroy();

    private:
        Device(VkqType* type)
            : type(type)
        {
        }

        VkqType* type = nullptr;
    };
} // namespace vkq
