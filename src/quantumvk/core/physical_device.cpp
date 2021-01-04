#include "physical_device.hpp"

namespace vkq
{
    PhysicalDevice PhysicalDevice::create(const Instance& instance, vk::PhysicalDevice phdev)
    {
        return PhysicalDevice{instance, phdev};
    }

    void PhysicalDevice::reset()
    {
        instance = {};
        phdev = nullptr;
    }

    std::vector<vk::ExtensionProperties> PhysicalDevice::enumerateDeviceExtensionProperties(vk::Optional<const std::string> layerName = nullptr)
    {
        return phdev.enumerateDeviceExtensionProperties(layerName, instance.getInstanceDispatch());
    }

    vk::PhysicalDeviceProperties PhysicalDevice::getProperties()
    {
        return phdev.getProperties(instance.getInstanceDispatch());
    }

    std::vector<vk::QueueFamilyProperties> PhysicalDevice::getQueueFamilyProperties()
    {
        return phdev.getQueueFamilyProperties(instance.getInstanceDispatch());
    }

#ifdef VK_KHR_SURFACE_EXTENSION_NAME
    vk::Bool32 PhysicalDevice::getSurfaceSupportKHR(uint32_t queueFamilyIndex, vk::SurfaceKHR surface)
    {
        return phdev.getSurfaceSupportKHR(queueFamilyIndex, surface, instance.getInstanceDispatch());
    }
#endif
}