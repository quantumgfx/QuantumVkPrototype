#include "queue.hpp"

namespace vkq
{
    QueueFamily QueueFamily::create(const Device& device, uint32_t queueFamilyIndex)
    {
        return QueueFamily{device, queueFamilyIndex};
    }

    void QueueFamily::reset()
    {
        device = {};
        queueFamilyIndex = 0;
    }

    vk::Bool32 QueueFamily::getSurfaceSupportKHR(vk::SurfaceKHR surface)
    {
        return device.vkPhysicalDevice().getSurfaceSupportKHR(queueFamilyIndex, surface, device.getDeviceDispatch());
    }

    Queue Queue::create(const Device& device, uint32_t queueFamilyIndex, uint32_t queueIndex)
    {
        return Queue{device, device.getQueue(queueFamilyIndex, queueIndex)};
    }

    Queue Queue::create2(const Device& device, const vk::DeviceQueueInfo2& queueInfo)
    {
        return Queue{device, device.getQueue2(queueInfo)};
    }

} // namespace vkq
