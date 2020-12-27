#include "loader.hpp"
#include "impl.hpp"

namespace vkq
{

    Loader createLoader(PFN_vkGetInstanceProcAddr getInstanceProcAddr)
    {
        return Loader::create(getInstanceProcAddr);
    }

    Loader Loader::create(PFN_vkGetInstanceProcAddr getInstanceProcAddr)
    {
        LoaderImpl* impl = new LoaderImpl();
        impl->dispatch.init(getInstanceProcAddr);
        return Loader{impl};
    }

    void Loader::destroy()
    {
        delete impl;
        impl = nullptr;
    }

    PFN_vkGetInstanceProcAddr Loader::getInstanceProcAddrLoader() const
    {
        return impl->dispatch.vkGetInstanceProcAddr;
    }

    const vk::DispatchLoaderDynamic& Loader::getGlobalDispatch() const
    {
        return impl->dispatch;
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

    std::vector<vk::LayerProperties> Loader::enumerateInstanceLayerProperties() const
    {
        return vk::enumerateInstanceLayerProperties(impl->dispatch);
    }

    std::vector<vk::ExtensionProperties> Loader::enumerateInstanceExtensionProperties(vk::Optional<const std::string> layerName) const
    {
        return vk::enumerateInstanceExtensionProperties(layerName, impl->dispatch);
    }

    Instance Loader::createInstance(const vk::InstanceCreateInfo& createInfo)
    {
        return Instance::create(impl->dispatch.vkGetInstanceProcAddr, createInfo);
    }
} // namespace vkq