#include "physicalDevice.hpp"

namespace vkq
{
    PhysicalDevice PhysicalDevice::create(Instance instance, vk::PhysicalDevice physicalDevice)
    {
        return PhysicalDevice(instance, physicalDevice);
    }
}