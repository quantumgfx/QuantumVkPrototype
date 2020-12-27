#pragma once

#include "../base/vk.hpp"

#include "instance.hpp"
#include "loader.hpp"
#include "physical_device.hpp"

namespace vkq
{
    struct LoaderImpl
    {
    private:
        LoaderImpl() = default;
        ~LoaderImpl() = default;

        vk::DispatchLoaderDynamic dispatch;

        friend class Loader;
    };

    struct InstanceImpl
    {
    private:
        InstanceImpl() = default;
        ~InstanceImpl() = default;

        vk::Instance instance;
        vk::DispatchLoaderDynamic dispatch = {};

        vk::ApplicationInfo appInfo = {};
        std::vector<const char*> enabledLayers;
        std::vector<const char*> enabledExtensions;

        std::vector<PhysicalDevice> physicalDevices;

        friend class Loader;
        friend class Instance;
    };

    struct PhysicalDeviceImpl
    {
    private:
        PhysicalDeviceImpl() = default;
        ~PhysicalDeviceImpl() = default;

        Instance instance;
        vk::PhysicalDevice phdev;

        friend class Instance;
        friend class PhysicalDevice;
    };

} // namespace vkq
