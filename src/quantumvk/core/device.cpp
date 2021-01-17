#include "device.hpp"

namespace vkq
{
    explicit Device::Device(Device::Impl* impl)
    {
    }

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
#ifdef VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME
            if (strcmp(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, extension) == 0)
                impl->extensionSupport.getMemoryRequirements2KHR = true;
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

        impl->memProps = phdev.getMemoryProperties(impl->dispatch);

        return Device{impl};
    }

    void Device::destroy()
    {
        impl_->device.destroy(nullptr, impl_->dispatch);

        delete impl_;
        impl_ = nullptr;
    }

    void Device::allocateCommandBuffers(vk::CommandPool commandPool, uint32_t commandBufferCount, vk::CommandBuffer* commandBuffers, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next = {}) const
    {
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.pNext = next;
        allocInfo.commandPool = commandPool;
        allocInfo.level = level;
        allocInfo.commandBufferCount = commandBufferCount;

        vk::Result result = impl_->device.allocateCommandBuffers(&allocInfo, commandBuffers, impl_->dispatch);

        switch (result)
        {
        case vk::Result::eErrorOutOfHostMemory:
            throw vk::OutOfHostMemoryError("vkq::Device::allocateCommandBuffers");
        case vk::Result::eErrorOutOfDeviceMemory:
            throw vk::OutOfDeviceMemoryError("vkq::Device::allocateCommandBuffers");
        default:
            break;
        }
    }

    bool Device::isDeviceExtensionEnabled(const char* extensionName) const
    {
        for (const char* extension : impl_->enabledExtensions)
            if (strcmp(extensionName, extension) == 0)
                return true;
        return false;
    }

    const Device::ExtensionSupport& Device::extensionSupport() const
    {
        return impl_->extensionSupport;
    }

    const vk::PhysicalDeviceMemoryProperties& Device::memoryProperties() const
    {
        return impl_->memProps;
    }

    vk::MemoryType Device::memoryTypeProperties(uint32_t memoryTypeIndex) const
    {
        return impl_->memProps.memoryTypes[memoryTypeIndex];
    }

    vk::MemoryHeap Device::memoryHeapProperties(uint32_t memoryHeapIndex) const
    {
        return impl_->memProps.memoryHeaps[memoryHeapIndex];
    }

    const vk::DispatchLoaderDynamic& Device::dispatch() const
    {
        return impl_->dispatch;
    }

    vk::Instance Device::vkInstance() const
    {
        return impl_->instance;
    }

    vk::PhysicalDevice Device::vkPhysicalDevice() const
    {
        return impl_->phdev;
    }

    vk::Device Device::vkDevice() const
    {
        return impl_->device;
    }

    vk::Device Device::vkHandle() const
    {
        return impl_->device;
    }

    Device::operator vk::Device() const
    {
        return impl_->device;
    }

} // namespace vkq