#pragma once

#include "../base/vk.hpp"
#include "../core/instance.hpp"
#include "../core/loader.hpp"

namespace vkq
{
    class InstanceFactory
    {
    public:
        explicit InstanceFactory(Loader loader);
        ~InstanceFactory() = default;

        InstanceFactory& requireApiVersion(uint32_t version);
        InstanceFactory& desireApiVersion(uint32_t version);
        InstanceFactory& requireApiVersion(uint32_t major, uint32_t minor, uint32_t patch = 0) { return requireApiVersion(VK_MAKE_VERSION(major, minor, patch)); }
        InstanceFactory& desireApiVersion(uint32_t major, uint32_t minor, uint32_t patch = 0) { return desireApiVersion(VK_MAKE_VERSION(major, minor, patch)); }

        InstanceFactory& setAppName(const char* name)
        {
            appName = name;
            return *this;
        }

        InstanceFactory& setEngineName(const char* name)
        {
            engineName = name;
            return *this;
        }

        InstanceFactory& setAppVersion(uint32_t version)
        {
            appVersion = version;
            return *this;
        }

        InstanceFactory& setEngineVersion(uint32_t version)
        {
            engineVersion = version;
            return *this;
        }

        InstanceFactory& setAppVersion(uint32_t major, uint32_t minor, uint32_t patch = 0) { return setAppVersion(VK_MAKE_VERSION(major, minor, patch)); }
        InstanceFactory& setEngineVersion(uint32_t major, uint32_t minor, uint32_t patch = 0) { return setEngineVersion(VK_MAKE_VERSION(major, minor, patch)); }

        InstanceFactory& enableLayer(const char* layerName);
        InstanceFactory& enableExtension(const char* extensionName);

        InstanceFactory& enableLayers(const std::vector<const char*>& layerNames);
        InstanceFactory& enableExtensions(const std::vector<const char*>& extensionNames);

        Instance build();

    private:
        const char* appName = nullptr;
        const char* engineName = nullptr;
        uint32_t appVersion = 0;
        uint32_t engineVersion = 0;
        uint32_t requiredApiVersion = VK_MAKE_VERSION(1, 0, 0);
        uint32_t desiredApiVersion = VK_MAKE_VERSION(1, 0, 0);

        std::vector<const char*> layers;
        std::vector<const char*> extensions;

        Loader loader;
    };
} // namespace vkq