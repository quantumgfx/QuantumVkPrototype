#include "loader.hpp"

namespace vkq
{
    explicit Loader::Loader(Loader::Impl* impl_)
        : impl_(impl_)
    {
    }

    Loader Loader::create(PFN_vkGetInstanceProcAddr getInstanceProcAddr)
    {
        Loader::Impl* impl = new Loader::Impl();
        impl->dispatch.init(getInstanceProcAddr);
        return Loader{impl};
    }

    void Loader::destroy()
    {
        delete impl_;
        impl_ = nullptr;
    }

    uint32_t Loader::enumerateInstanceVersion() const
    {
#ifdef VK_VERSION_1_1

        if (impl_->dispatch.vkGetInstanceProcAddr)
        {
            return vk::enumerateInstanceVersion(impl_->dispatch);
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
        return vk::enumerateInstanceLayerProperties(impl_->dispatch);
    }

    std::vector<vk::ExtensionProperties> Loader::enumerateInstanceExtensionProperties(vk::Optional<const std::string> layerName) const
    {
        return vk::enumerateInstanceExtensionProperties(layerName, impl_->dispatch);
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

    const vk::DispatchLoaderDynamic& Loader::dispatch() const
    {
        return impl_->dispatch;
    }

    PFN_vkGetInstanceProcAddr Loader::instanceProcAddrLoader() const
    {
        return impl_->dispatch.vkGetInstanceProcAddr;
    }
} // namespace vkq