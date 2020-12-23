#pragma once

#include "instance.hpp"
#include "vk.hpp"

namespace vkq
{

    class PhysicalDevice
    {
    public:
        PhysicalDevice() = default;
        ~PhysicalDevice() = default;

    public:
        PhysicalDevice(Instance instance, vk::PhysicalDevice physicalDevice)
            : instance(instance), physicalDevice(physicalDevice)
        {
        }

        Instance getInstance() const { return instance; }
        const vk::DispatchLoaderDynamic& getInstanceDispatch() const { return instance.getInstanceDispatch(); }

        vk::PhysicalDevice vkPhysicalDevice() const { return physicalDevice; }
        vk::PhysicalDevice vkHandle() const { return physicalDevice; }
        operator vk::PhysicalDevice() const { return physicalDevice; }

        explicit operator bool() const noexcept { return static_cast<bool>(physicalDevice); }

    private:
        Instance instance;
        vk::PhysicalDevice physicalDevice;
    };

} // namespace vkq