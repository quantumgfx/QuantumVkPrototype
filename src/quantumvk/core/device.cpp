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