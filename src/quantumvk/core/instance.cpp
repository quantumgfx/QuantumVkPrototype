#include "instance.hpp"
#include "impl.hpp"

#include <cstring>

namespace vkq
{
    ////////////////////////////////
    // Instance ////////////////////
    ////////////////////////////////

    Instance Instance::create(PFN_vkGetInstanceProcAddr getInstanceProcAddr, const vk::InstanceCreateInfo& createInfo)
    {
        InstanceImpl* impl = new InstanceImpl();
        impl->dispatch.init(getInstanceProcAddr);
        impl->instance = vk::createInstance(createInfo, nullptr, impl->dispatch);
        impl->dispatch.init(impl->instance);
        impl->appInfo = *createInfo.pApplicationInfo;

        impl->enabledLayers.insert(impl->enabledLayers.begin(), createInfo.ppEnabledLayerNames, createInfo.ppEnabledLayerNames + createInfo.enabledLayerCount);
        impl->enabledExtensions.insert(impl->enabledExtensions.begin(), createInfo.ppEnabledExtensionNames, createInfo.ppEnabledExtensionNames + createInfo.enabledExtensionCount);

        std::vector<vk::PhysicalDevice> vkPhysicalDevices = impl->instance.enumeratePhysicalDevices(impl->dispatch);

        impl->physicalDevices.reserve(vkPhysicalDevices.size());

        for (auto phdev : vkPhysicalDevices)
        {
            PhysicalDeviceImpl* phdevImpl = new PhysicalDeviceImpl();
            phdevImpl->instance = Instance(impl);
            phdevImpl->phdev = phdev;
            impl->physicalDevices.emplace_back(phdevImpl);
        }

        return Instance{impl};
    }

    void Instance::destroy()
    {
        for (auto phdev : impl->physicalDevices)
        {
            PhysicalDeviceImpl* phdevImpl = phdev.getImpl();
            delete phdevImpl;
        }

        impl->instance.destroy(nullptr, impl->dispatch);

        delete impl;
        impl = nullptr;
    }

    std::vector<PhysicalDevice> Instance::enumeratePhysicalDevices() const
    {
        return impl->physicalDevices;
    }

#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME

    DebugUtilsMessengerEXT Instance::createDebugUtilsMessengerEXT(const vk::DebugUtilsMessengerCreateInfoEXT& createInfo, vk::Optional<const vk::AllocationCallbacks> allocator)
    {
        return DebugUtilsMessengerEXT{impl->instance.createDebugUtilsMessengerEXT(createInfo, allocator, impl->dispatch)};
    }

    void Instance::destroyDebugUtilsMessengerEXT(DebugUtilsMessengerEXT messenger, vk::Optional<const vk::AllocationCallbacks> allocator)
    {
        impl->instance.destroyDebugUtilsMessengerEXT(static_cast<vk::DebugUtilsMessengerEXT>(messenger), allocator, impl->dispatch);
    }

#endif

    const vk::ApplicationInfo& Instance::getApplicationInfo()
    {
        return impl->appInfo;
    }

    bool Instance::isInstanceExtensionEnabled(const char* extensionName)
    {
        for (const char* extension : impl->enabledExtensions)
            if (strcmp(extensionName, extension) == 0)
                return true;
        return false;
    }

    bool Instance::isLayerEnabled(const char* layerName)
    {
        for (const char* layer : impl->enabledLayers)
            if (strcmp(layerName, layer) == 0)
                return true;
        return false;
    }

    PFN_vkGetInstanceProcAddr Instance::getInstanceProcAddrLoader() const
    {
        return impl->dispatch.vkGetInstanceProcAddr;
    }

    const vk::DispatchLoaderDynamic& Instance::getInstanceDispatch() const
    {
        return impl->dispatch;
    }

    vk::Instance Instance::vkInstance() const
    {
        return impl->instance;
    }

    vk::Instance Instance::vkHandle() const
    {
        return impl->instance;
    }

    Instance::operator vk::Instance() const
    {
        return impl->instance;
    }

} // namespace vkq