#include "queue.hpp"

namespace vkq
{
    explicit QueueFamily::QueueFamily(Device device_, uint32_t queueFamilyIndex_)
        : device_(device_), queueFamilyIndex_(queueFamilyIndex_)
    {
    }

    QueueFamily QueueFamily::create(const Device& device, uint32_t queueFamilyIndex)
    {
        return QueueFamily{device, queueFamilyIndex};
    }

    void QueueFamily::reset()
    {
        device_ = {};
        queueFamilyIndex_ = 0;
    }

    vk::Bool32 QueueFamily::getSurfaceSupportKHR(vk::SurfaceKHR surface)
    {
        return device_.vkPhysicalDevice().getSurfaceSupportKHR(queueFamilyIndex_, surface, device_.dispatch());
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
