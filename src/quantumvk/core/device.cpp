#include "device.hpp"

namespace vkq
{

    Device Device::create(Instance instance, vk::PhysicalDevice physicalDevice, const vk::DeviceCreateInfo& createInfo)
    {
        Device device(new VkqType());

        device.type->instance = instance.vkInstance();
        device.type->physicalDevice = physicalDevice;
        device.type->dispatch = instance.getInstanceDispatch();
        device.type->device = physicalDevice.createDevice(createInfo, nullptr, device.type->dispatch);
        device.type->dispatch.init(device.type->device);

        return device;
    }

    Device Device::create(PFN_vkGetInstanceProcAddr getInstanceProcAddr, vk::Instance instance, vk::PhysicalDevice physicalDevice, const vk::DeviceCreateInfo& createInfo)
    {
        Device device(new VkqType());

        device.type->instance = instance;
        device.type->physicalDevice = physicalDevice;
        device.type->dispatch.init(getInstanceProcAddr);
        device.type->dispatch.init(instance);
        device.type->device = physicalDevice.createDevice(createInfo, nullptr, device.type->dispatch);
        device.type->dispatch.init(device.type->device);

        return device;
    }

    void Device::destroy()
    {
        type->device.destroy(nullptr, type->dispatch);

        delete type;
        type = nullptr;
    }

} // namespace vkq