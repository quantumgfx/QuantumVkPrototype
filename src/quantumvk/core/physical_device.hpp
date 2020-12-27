#pragma once

#include "../base/vk.hpp"
#include "forward.hpp"

#include <string>

namespace vkq
{
    struct PhysicalDeviceImpl;

    class PhysicalDevice
    {
    public:
        PhysicalDevice() = default;
        ~PhysicalDevice() = default;

        PhysicalDevice(PhysicalDeviceImpl* impl)
            : impl(impl)
        {
        }

    public:
        std::vector<vk::ExtensionProperties> enumerateDeviceExtensionProperties(vk::Optional<const std::string> layerName = nullptr);

        vk::PhysicalDevice vkPhysicalDevice() const;
        vk::PhysicalDevice vkHandle() const;
        operator vk::PhysicalDevice() const;

        PhysicalDeviceImpl* getImpl() const { return impl; }

    private:
        PhysicalDeviceImpl* impl;
    };

} // namespace vkq