#include "loader.hpp"

namespace vkq
{

    Loader Loader::create(PFN_vkGetInstanceProcAddr getInstanceProcAddr)
    {
        Loader::Impl* impl = new Loader::Impl();
        impl->dispatch.init(getInstanceProcAddr);
        return Loader{impl};
    }

    void Loader::destroy()
    {
        delete impl;
        impl = nullptr;
    }

    uint32_t Loader::enumerateInstanceVersion() const
    {
#ifdef VK_VERSION_1_1

        if (impl->dispatch.vkGetInstanceProcAddr)
        {
            return vk::enumerateInstanceVersion(impl->dispatch);
        }
        else
        {
            return VK_MAKE_VERSION(1, 0, 0);
        }

#else
        return VK_MAKE_VERSION(1, 0, 0);
#endif
    }

    std::vector<vk::LayerProperties> Loader::enumerateLayerProperties() const
    {
        return vk::enumerateInstanceLayerProperties(impl->dispatch);
    }

    std::vector<vk::ExtensionProperties> Loader::enumerateInstanceExtensionProperties(vk::Optional<const std::string> layerName) const
    {
        return vk::enumerateInstanceExtensionProperties(layerName, impl->dispatch);
    }

    bool Loader::isLayerSupported(const char* layerName) const
    {
        for (auto& queriedLayer : enumerateLayerProperties())
        {
            if (strcmp(queriedLayer.layerName, layerName) == 0)
                return true;
        }
        return false;
    }

    bool Loader::isInstanceExtensionSupported(const char* extensionName, vk::Optional<const std::string> layerName) const
    {
        for (auto& quiriedExtension : enumerateInstanceExtensionProperties(layerName))
        {
            if (strcmp(quiriedExtension.extensionName, extensionName) == 0)
                return true;
        }
        return false;
    }

    const vk::DispatchLoaderDynamic& Loader::getGlobalDispatch() const
    {
        return impl->dispatch;
    }

    PFN_vkGetInstanceProcAddr Loader::getInstanceProcAddrLoader() const
    {
        return impl->dispatch.vkGetInstanceProcAddr;
    }
} // namespace vkq