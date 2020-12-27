#include "physical_device.hpp"
#include "impl.hpp"

namespace vkq
{

    std::vector<vk::ExtensionProperties> PhysicalDevice::enumerateDeviceExtensionProperties(vk::Optional<const std::string> layerName = nullptr)
    {
        return impl->phdev.enumerateDeviceExtensionProperties(layerName, impl->instance.getInstanceDispatch());
    }

    vk::PhysicalDevice PhysicalDevice::vkPhysicalDevice() const
    {
        return impl->phdev;
    }

    vk::PhysicalDevice PhysicalDevice::vkHandle() const
    {
        return impl->phdev;
    }

    PhysicalDevice::operator vk::PhysicalDevice() const
    {
        return impl->phdev;
    }
}