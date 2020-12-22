#pragma once

#include "vk.hpp"
#include "instance.hpp"

namespace vkq
{

    class PhysicalDevice
    {
    public:
        PhysicalDevice() = default;
        ~PhysicalDevice() = default;

    public:
        static PhysicalDevice create(Instance instance, vk::PhysicalDevice physicalDevice);

        const vk::DispatchLoaderDynamic& getInstanceDispatch() const { return instance.getInstanceDispatch(); }

        vk::PhysicalDevice vkPhysicalDevice() const { return physicalDevice; }
        vk::PhysicalDevice vkHandle() const { return physicalDevice; }
        operator vk::PhysicalDevice() const { return physicalDevice; }

        explicit operator bool() const noexcept { return static_cast<bool>(physicalDevice); }

    private:
        PhysicalDevice(Instance instance, vk::PhysicalDevice physicalDevice)
            : instance(instance), physicalDevice(physicalDevice)
        {
        }

        Instance instance;
        vk::PhysicalDevice physicalDevice;
    };

} // namespace vkq