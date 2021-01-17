#include "instance.hpp"

#include <cstring>

namespace vkq
{
    ////////////////////////////////
    // Instance ////////////////////
    ////////////////////////////////

    explicit Instance::Instance(Instance::Impl* impl_)
        : impl_(impl_)
    {
    }

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
#ifdef VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
            if (strcmp(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, extension) == 0)
                impl->extensionSupport.getPhysicalDeviceProperties2KHR = true;
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
        impl_->instance.destroy(nullptr, impl_->dispatch);

        delete impl_;
        impl_ = nullptr;
    }

#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME

    vk::DebugUtilsMessengerEXT Instance::createDebugUtilsMessengerEXT(const vk::DebugUtilsMessengerCreateInfoEXT& createInfo, vk::Optional<const vk::AllocationCallbacks> allocator)
    {
        return impl_->instance.createDebugUtilsMessengerEXT(createInfo, allocator, impl_->dispatch);
    }

    void Instance::destroyDebugUtilsMessengerEXT(vk::DebugUtilsMessengerEXT messenger, vk::Optional<const vk::AllocationCallbacks> allocator)
    {
        impl_->instance.destroyDebugUtilsMessengerEXT(messenger, allocator, impl_->dispatch);
    }

#endif

    std::vector<vk::PhysicalDevice> Instance::enumeratePhysicalDevices() const
    {
        return impl_->instance.enumeratePhysicalDevices(impl_->dispatch);
    }

    const vk::ApplicationInfo& Instance::applicationInfo() const
    {
        return impl_->appInfo;
    }

    bool Instance::isInstanceExtensionEnabled(const char* extensionName) const
    {
        for (const char* extension : impl_->enabledExtensions)
            if (strcmp(extensionName, extension) == 0)
                return true;
        return false;
    }

    bool Instance::isLayerEnabled(const char* layerName) const
    {
        for (const char* layer : impl_->enabledLayers)
            if (strcmp(layerName, layer) == 0)
                return true;
        return false;
    }

    const Instance::ExtensionSupport& Instance::extensionSupport() const
    {
        return impl_->extensionSupport;
    }

    const vk::DispatchLoaderDynamic& Instance::dispatch() const
    {
        return impl_->dispatch;
    }

    PFN_vkGetInstanceProcAddr Instance::instanceProcAddrLoader() const
    {
        return impl_->dispatch.vkGetInstanceProcAddr;
    }

    vk::Instance Instance::vkInstance() const
    {
        return impl_->instance;
    }

    vk::Instance Instance::vkHandle() const
    {
        return impl_->instance;
    }

    Instance::operator vk::Instance() const
    {
        return impl_->instance;
    }

} // namespace vkq