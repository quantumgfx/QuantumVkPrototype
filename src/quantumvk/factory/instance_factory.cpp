#include "instance_factory.hpp"

#include <cstring>

namespace vkq
{
    /////////////////////////////////
    // Instance Factory /////////////
    /////////////////////////////////

    InstanceFactory::InstanceFactory(Loader loader)
        : loader(loader)
    {
    }

    InstanceFactory& InstanceFactory::requireApiVersion(uint32_t version)
    {
        if (version < VK_MAKE_VERSION(1, 0, 0))
            return *this;
        requiredApiVersion = version;
        return *this;
    }

    InstanceFactory& InstanceFactory::desireApiVersion(uint32_t version)
    {
        if (version < VK_MAKE_VERSION(1, 0, 0))
            return *this;
        desiredApiVersion = version;
        return *this;
    }

    InstanceFactory& InstanceFactory::enableLayer(const char* layerName)
    {
        if (!layerName)
            return *this;
        layers.push_back(layerName);
        return *this;
    }

    InstanceFactory& InstanceFactory::enableExtension(const char* extensionName)
    {
        if (!extensionName)
            return *this;
        extensions.push_back(extensionName);
        return *this;
    }

    InstanceFactory& InstanceFactory::enableLayers(const std::vector<const char*>& layerNames)
    {
        for (auto layerName : layerNames)
        {
            if (!layerName)
                continue;
            layers.push_back(layerName);
        }

        return *this;
    }

    InstanceFactory& InstanceFactory::enableExtensions(const std::vector<const char*>& extensionNames)
    {
        for (auto extensionName : extensionNames)
        {
            if (!extensionName)
                continue;
            extensions.push_back(extensionName);
        }

        return *this;
    }

    static bool checkLayerSupported(const std::vector<vk::LayerProperties>& queriedlayers, const char* layer)
    {
        for (auto& queriedLayer : queriedlayers)
        {
            if (strcmp(queriedLayer.layerName, layer) == 0)
                return true;
        }
        return false;
    }

    static bool checkInstanceExtensionSupported(const std::vector<vk::ExtensionProperties>& queriedExtensions, const char* extension)
    {
        for (auto& queriedExtension : queriedExtensions)
        {
            if (strcmp(queriedExtension.extensionName, extension) == 0)
                return true;
        }
        return false;
    }

    Instance InstanceFactory::build()
    {

        vk::ApplicationInfo appInfo{};
        vk::InstanceCreateInfo createInfo{};

        // Retrieve the highest available instance version
        uint32_t apiVersion = loader.enumerateInstanceVersion();

        // If the highest available version is less than the required version, throw an error
        if (requiredApiVersion > apiVersion)
            throw std::runtime_error("Required API version is not available");

        // If the requested version is available, use that. Else just use the minimum required version
        if ((requiredApiVersion < desiredApiVersion) && (desiredApiVersion <= apiVersion))
            apiVersion = desiredApiVersion;
        else
            apiVersion = requiredApiVersion;

        appInfo.applicationVersion = appVersion;
        appInfo.engineVersion = engineVersion;
        appInfo.pApplicationName = appName != nullptr ? appName : "";
        appInfo.pEngineName = engineName != nullptr ? engineName : "";
        appInfo.apiVersion = apiVersion;

        {
            std::vector<vk::LayerProperties> queriedLayers = loader.enumerateLayerProperties();

            for (const char* layer : layers)
                if (!checkLayerSupported(queriedLayers, layer))
                    throw std::runtime_error("Layer is enabled but not available");
        }

        {
            std::vector<vk::ExtensionProperties> queriedExtensions = loader.enumerateInstanceExtensionProperties();

            for (const char* extension : extensions)
                if (!checkInstanceExtensionSupported(queriedExtensions, extension))
                    throw std::runtime_error("Extension is enabled but not available");
        }

        vk::InstanceCreateInfo createInfo{};
        createInfo.pApplicationInfo = &appInfo;
        createInfo.setPApplicationInfo(&appInfo);
        createInfo.setPEnabledLayerNames(layers);
        createInfo.setPEnabledExtensionNames(extensions);

        return Instance::create(loader, createInfo);
    }
} // namespace vkq