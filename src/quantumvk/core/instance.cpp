#include "instance.hpp"

#include <cstring>

namespace vkq
{
    ////////////////////////////////
    // Instance ////////////////////
    ////////////////////////////////

    Instance Instance::create(PFN_vkGetInstanceProcAddr getInstanceProcAddr, const vk::InstanceCreateInfo& createInfo)
    {
        Instance::Impl* impl = new Instance::Impl();
        impl->dispatch.init(getInstanceProcAddr);
        impl->instance = vk::createInstance(createInfo, nullptr, impl->dispatch);
        impl->dispatch.init(impl->instance);
        impl->appInfo = *createInfo.pApplicationInfo;

        impl->enabledLayers.insert(impl->enabledLayers.begin(), createInfo.ppEnabledLayerNames, createInfo.ppEnabledLayerNames + createInfo.enabledLayerCount);
        impl->enabledExtensions.insert(impl->enabledExtensions.begin(), createInfo.ppEnabledExtensionNames, createInfo.ppEnabledExtensionNames + createInfo.enabledExtensionCount);

        for (const char* extension : impl->enabledExtensions)
        {
#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME
            if (strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, extension) == 0)
                impl->extensionSupport.debugUtilsEXT = true;
#endif
#ifdef VK_KHR_SURFACE_EXTENSION_NAME
            if (strcmp(VK_KHR_SURFACE_EXTENSION_NAME, extension) == 0)
                impl->extensionSupport.surfaceKHR = true;
#endif
        }

        return Instance{impl};
    }

    void Instance::destroy()
    {
        impl->instance.destroy(nullptr, impl->dispatch);

        delete impl;
        impl = nullptr;
    }

#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME

    vk::DebugUtilsMessengerEXT Instance::createDebugUtilsMessengerEXT(const vk::DebugUtilsMessengerCreateInfoEXT& createInfo, vk::Optional<const vk::AllocationCallbacks> allocator)
    {
        return impl->instance.createDebugUtilsMessengerEXT(createInfo, allocator, impl->dispatch);
    }

    void Instance::destroyDebugUtilsMessengerEXT(vk::DebugUtilsMessengerEXT messenger, vk::Optional<const vk::AllocationCallbacks> allocator)
    {
        impl->instance.destroyDebugUtilsMessengerEXT(messenger, allocator, impl->dispatch);
    }

#endif

    std::vector<vk::PhysicalDevice> Instance::enumeratePhysicalDevices() const
    {
        return impl->instance.enumeratePhysicalDevices(impl->dispatch);
    }

    const vk::ApplicationInfo& Instance::getApplicationInfo() const
    {
        return impl->appInfo;
    }

    bool Instance::isInstanceExtensionEnabled(const char* extensionName) const
    {
        for (const char* extension : impl->enabledExtensions)
            if (strcmp(extensionName, extension) == 0)
                return true;
        return false;
    }

    bool Instance::isLayerEnabled(const char* layerName) const
    {
        for (const char* layer : impl->enabledLayers)
            if (strcmp(layerName, layer) == 0)
                return true;
        return false;
    }

    const Instance::ExtensionSupport& Instance::getInstanceExtensionSupport() const
    {
        return impl->extensionSupport;
    }

    const vk::DispatchLoaderDynamic& Instance::getInstanceDispatch() const
    {
        return impl->dispatch;
    }

    PFN_vkGetInstanceProcAddr Instance::getInstanceProcAddrLoader() const
    {
        return impl->dispatch.vkGetInstanceProcAddr;
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