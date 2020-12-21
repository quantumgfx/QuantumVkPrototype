#pragma once

#include "../headers/vk.hpp"
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

    private:

        PhysicalDevice(Instance instance, vk::PhysicalDevice physicalDevice)
            : instance(instance), physicalDevice(physicalDevice)
        {
        }

        Instance instance;
        vk::PhysicalDevice physicalDevice;
    };

}