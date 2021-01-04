#include "physical_device_selector.hpp"

namespace vkq
{

    PhysicalDeviceSelector::PhysicalDeviceSelector(Instance instance)
        : instance(instance)
    {
    }

#ifdef VK_KHR_SURFACE_EXTENSION_NAME

    PhysicalDeviceSelector::PhysicalDeviceSelector(Instance instance, vk::SurfaceKHR surface)
        : instance(instance), presentSurface(surface)
    {
    }

#endif

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

    PhysicalDeviceSelector& PhysicalDeviceSelector::setSurfaceKHR(vk::SurfaceKHR surface)
    {
        presentSurface = surface;
        return *this;
    }

    PhysicalDeviceSelector& PhysicalDeviceSelector::setSupportSurfaceKHR(bool support)
    {
        presentSupportRequired = support;
        return *this;
    }

    PhysicalDeviceSelector& PhysicalDeviceSelector::setDesiredVersion(float weight, uint32_t version)
    {
        desiredVersionWeight = weight;
        desiredVersion = version;
        return *this;
    }

    PhysicalDeviceSelector& PhysicalDeviceSelector::requestExtension(float weight, const char* extensionName)
    {
        requestedExtensions.emplace_back(extensionName, weight);
        return *this;
    }

    PhysicalDeviceSelector& PhysicalDeviceSelector::requestExtensions(float weight, const std::vector<const char*>& extensionNames)
    {
        requestedExtensions.reserve(requestedExtensions.size() + extensionNames.size());
        for (auto extensionName : extensionNames)
            requestedExtensions.emplace_back(extensionName, weight);
        return *this;
    }

    PhysicalDeviceSelector& PhysicalDeviceSelector::preferPhysicalDeviceType(float weight, vk::PhysicalDeviceType type)
    {
        for (auto& typePref : typePreferences)
        {
            if (typePref.first == type)
            {
                typePref.second = weight;
                return *this;
            }
        }

        typePreferences.emplace_back(type, weight);
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

    PhysicalDevice PhysicalDeviceSelector::select()
    {
        std::vector<vk::PhysicalDevice> candidates;

        for (vk::PhysicalDevice candidate : instance.enumeratePhysicalDevices())
        {
            // Requirements involving physicalDeviceProps
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
            }
            // Requirements involving quried extensions
            {
                // Physical Device must support all required extensions
                std::vector<vk::ExtensionProperties> queriedExtensions = candidate.enumerateDeviceExtensionProperties(nullptr, instance.getInstanceDispatch());

                bool extensionsSupported = true;

                for (const char* extension : requiredExtensions)
                    extensionsSupported = (extensionsSupported && checkDeviceExtensionSupported(queriedExtensions, extension));

                if (!extensionsSupported)
                    continue;
            }

            // Queue requirements
            {
                std::vector<vk::QueueFamilyProperties> queueProps = candidate.getQueueFamilyProperties(instance.getInstanceDispatch());

#ifdef VK_KHR_SURFACE_EXTENSION_NAME
                // If requested, surface presentation must be supported by physical device
                bool surfaceSupported = false;

                if (static_cast<bool>(presentSurface))
                    for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueProps.size(); queueFamilyIndex++)
                        surfaceSupported = surfaceSupported && (VK_TRUE == candidate.getSurfaceSupportKHR(queueFamilyIndex, presentSurface, instance.getInstanceDispatch()));

                if (presentSupportRequired && !surfaceSupported)
                    continue;
#endif
            }

            // vk::PhysicalDeviceMemoryProperties memProps = candidate.getMemoryProperties(instance.getInstanceDispatch());

            // Physical Device passes all requirements, and thus can be considered a candidate.
            candidates.push_back(candidate);
        }

        if (candidates.size() == 0)
        {
            throw std::runtime_error("No suitable gpu found");
        }

        bool set = false;
        float bestWeight = 0;
        vk::PhysicalDevice bestCandidate;

        for (vk::PhysicalDevice candidate : candidates)
        {
            float candidateWeight = 0.0f;

            // Preferences involving physicalDeviceProperties
            {
                vk::PhysicalDeviceProperties props = candidate.getProperties(instance.getInstanceDispatch());

                if (props.apiVersion >= desiredVersion)
                    candidateWeight += desiredVersionWeight;

                for (auto type : typePreferences)
                {
                    if (type.first == props.deviceType)
                    {
                        candidateWeight += type.second;
                        break;
                    }
                }
            }

            // Prefernces involving extensions
            {
                std::vector<vk::ExtensionProperties> queriedExtensions = candidate.enumerateDeviceExtensionProperties(nullptr, instance.getInstanceDispatch());

                for (auto extension : requestedExtensions)
                    if (checkDeviceExtensionSupported(queriedExtensions, extension.first))
                        candidateWeight += extension.second;
            }

            if (candidateWeight >= bestWeight || !set)
            {
                set = true;
                bestWeight = candidateWeight;
                bestCandidate = candidate;
            }
        }

        return PhysicalDevice::create(instance, bestCandidate);
    }

} // namespace vkq