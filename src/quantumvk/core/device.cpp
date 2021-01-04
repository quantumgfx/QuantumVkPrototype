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

        return Device{impl};
    }

    void Device::destroy()
    {
        impl->device.destroy(nullptr, impl->dispatch);

        delete impl;
        impl = nullptr;
    }

    vk::CommandPool Device::createCommandPool(const vk::CommandPoolCreateInfo& createInfo) const
    {
        return impl->device.createCommandPool(createInfo, nullptr, impl->dispatch);
    }

    void Device::destroyCommandPool(vk::CommandPool commandPool) const
    {
        return impl->device.destroyCommandPool(commandPool, nullptr, impl->dispatch);
    }

    std::vector<vk::CommandBuffer> Device::allocateCommandBuffers(const vk::CommandBufferAllocateInfo& allocateInfo) const
    {
        return impl->device.allocateCommandBuffers(allocateInfo, impl->dispatch);
    }

    void Device::freeCommandBuffers(vk::CommandPool commandPool, const vk::ArrayProxy<const vk::CommandBuffer>& commandBuffers) const
    {
        impl->device.freeCommandBuffers(commandPool, commandBuffers, impl->dispatch);
    }

    void Device::resetCommandPool(vk::CommandPool commandPool, vk::CommandPoolResetFlags flags) const
    {
        impl->device.resetCommandPool(commandPool, flags, impl->dispatch);
    }
#ifdef VK_VERSION_1_1

    void Device::trimCommandPool(vk::CommandPool commandPool, vk::CommandPoolTrimFlags flags) const
    {
        impl->device.trimCommandPool(commandPool, flags, impl->dispatch);
    }

#endif

#ifdef VK_KHR_MAINTENANCE1_EXTENSION_NAME

    void Device::trimCommandPoolKHR(vk::CommandPool commandPool, vk::CommandPoolTrimFlagsKHR flags) const
    {
        impl->device.trimCommandPoolKHR(commandPool, flags);
    }

#endif

#ifdef VK_VERSION_1_1

    vk::Queue Device::getQueue2(const vk::DeviceQueueInfo2& queueInfo) const
    {
        return impl->device.getQueue2(queueInfo, impl->dispatch);
    }

#endif

    vk::Queue Device::getQueue(uint32_t queueFamilyIndex, uint32_t queueIndex) const
    {
        return impl->device.getQueue(queueFamilyIndex, queueIndex, impl->dispatch);
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