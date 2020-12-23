#include "physicalDeviceSelector.hpp"

namespace vkq
{

    PhysicalDeviceSelector::PhysicalDeviceSelector(Instance instance)
        : instance(instance)
    {
    }

    PhysicalDeviceSelector& PhysicalDeviceSelector::setMinimumVersion(uint32_t version)
    {
        if (version < VK_MAKE_VERSION(1, 0, 0))
            return *this;
        minimumVersion = version;
        return *this;
    }

    PhysicalDeviceSelector& PhysicalDeviceSelector::requireExtension(const char* extensionName)
    {
        if (!extensionName)
            return *this;
        requiredExtensions.push_back(extensionName);
        return *this;
    }

    PhysicalDeviceSelector& PhysicalDeviceSelector::requireExtensions(const std::vector<const char*>& extensionNames)
    {
        for (auto extensionName : extensionNames)
        {
            if (!extensionName)
                continue;
            requiredExtensions.push_back(extensionName);
        }
        return *this;
    }

    PhysicalDeviceSelector& PhysicalDeviceSelector::setAllowedPhysicalDeviceTypes(const std::vector<vk::PhysicalDeviceType>& types)
    {
        allowedTypes = types;
        return *this;
    }

    static bool checkDeviceExtensionSupported(const std::vector<vk::ExtensionProperties>& queriedExtensions, const char* extension)
    {
        for (auto& queriedExtension : queriedExtensions)
        {
            if (strcmp(queriedExtension.extensionName, extension) == 0)
                return true;
        }
        return false;
    }

    vk::PhysicalDevice PhysicalDeviceSelector::select()
    {
        std::vector<vk::PhysicalDevice> candidates;

        for (vk::PhysicalDevice candidate : instance.enumeratePhysicalDevices())
        {
            vk::PhysicalDeviceProperties props = candidate.getProperties(instance.getInstanceDispatch());

            // Physical Device must support minimum requested version of vulkan
            if (props.apiVersion < minimumVersion)
                continue;

            // Physical Device must be of an allowed type
            bool allowedType = false;

            for (vk::PhysicalDeviceType type : allowedTypes)
                if (props.deviceType == type)
                {
                    allowedType = true;
                    break;
                }

            if (!allowedType)
                continue;

            // Physical Device must support all required extensions
            std::vector<vk::ExtensionProperties>
                queriedExtensions = candidate.enumerateDeviceExtensionProperties(nullptr, instance.getInstanceDispatch());

            bool extensionsSupported = true;

            for (const char* extension : requiredExtensions)
                extensionsSupported = (extensionsSupported && checkDeviceExtensionSupported(queriedExtensions, extension));

            if (!extensionsSupported)
                continue;

            // vk::PhysicalDeviceMemoryProperties memProps = candidate.getMemoryProperties(instance.getInstanceDispatch());

            // Physical Device passes all requirements, and thus can be considered a candidate.
            candidates.push_back(candidate);
        }

        // TEMP
        return candidates.front();
    }

} // namespace vkq