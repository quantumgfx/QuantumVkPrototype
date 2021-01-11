#include "device.hpp"

namespace vkq
{

    Device Device::create(PFN_vkGetInstanceProcAddr getInstanceProcAddr, vk::Instance instance, vk::PhysicalDevice phdev, const vk::DeviceCreateInfo& createInfo)
    {
        Device::Impl* impl = new Device::Impl();
        impl->instance = instance;
        impl->phdev = phdev;
        impl->dispatch.init(getInstanceProcAddr);
        impl->dispatch.init(instance);
        impl->device = phdev.createDevice(createInfo, nullptr, impl->dispatch);
        impl->dispatch.init(impl->device);

        impl->enabledExtensions.insert(impl->enabledExtensions.begin(), createInfo.ppEnabledExtensionNames, createInfo.ppEnabledExtensionNames + createInfo.enabledExtensionCount);

        for (const char* extension : impl->enabledExtensions)
        {
#ifdef VK_KHR_BIND_MEMORY_2_EXTENSION_NAME
            if (strcmp(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME, extension) == 0)
                impl->extensionSupport.bindMemory2KHR = true;
#endif
#ifdef VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
            if (strcmp(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, extension) == 0)
                impl->extensionSupport.bufferDeviceAddressKHR = true;
#endif
#ifdef VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME
            if (strcmp(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME, extension) == 0)
                impl->extensionSupport.dedicatedAllocationKHR = true;
#endif
#ifdef VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME
            if (strcmp(VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME, extension) == 0)
                impl->extensionSupport.deviceCoherentMemoryAMD = true;
#endif
#ifdef VK_EXT_MEMORY_BUDGET_EXTENSION_NAME
            if (strcmp(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, extension) == 0)
                impl->extensionSupport.memoryBudgetEXT = true;
#endif
#ifdef VK_KHR_SWAPCHAIN_EXTENSION_NAME
            if (strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME, extension) == 0)
                impl->extensionSupport.swapchainKHR = true;
#endif
        }

        return Device{impl};
    }

    void Device::destroy()
    {
        impl->device.destroy(nullptr, impl->dispatch);

        delete impl;
        impl = nullptr;
    }

    bool Device::isDeviceExtensionEnabled(const char* extensionName) const
    {
        for (const char* extension : impl->enabledExtensions)
            if (strcmp(extensionName, extension) == 0)
                return true;
        return false;
    }

    const Device::ExtensionSupport& Device::getDeviceExtensionSupport() const
    {
        return impl->extensionSupport;
    }

    const vk::DispatchLoaderDynamic& Device::getDeviceDispatch() const
    {
        return impl->dispatch;
    }

    vk::Instance Device::vkInstance() const
    {
        return impl->instance;
    }

    vk::PhysicalDevice Device::vkPhysicalDevice() const
    {
        return impl->phdev;
    }

    vk::Device Device::vkDevice() const
    {
        return impl->device;
    }

    vk::Device Device::vkHandle() const
    {
        return impl->device;
    }

    Device::operator vk::Device() const
    {
        return impl->device;
    }

} // namespace vkq