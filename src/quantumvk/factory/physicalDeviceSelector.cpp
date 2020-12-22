#include "physicalDeviceSelector.hpp"

namespace vkq
{

    PhysicalDeviceSelector::PhysicalDeviceSelector(Instance instance)
        : instance(instance)
    {
    }

    PhysicalDeviceSelector& PhysicalDeviceSelector::setDesiredVersion(uint32_t version)
    {
        if (version < VK_MAKE_VERSION(1, 0, 0))
            return *this;
        desiredVersion = version;
        return *this;
    }

    PhysicalDeviceSelector& PhysicalDeviceSelector::setMinimumVersion(uint32_t version)
    {
        if (version < VK_MAKE_VERSION(1, 0, 0))
            return *this;
        minimumVersion = version;
        return *this;
    }

} // namespace vkq