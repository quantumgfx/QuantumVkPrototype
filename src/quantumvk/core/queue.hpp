#pragma once

#include "../base/vk.hpp"
#include "device.hpp"

namespace vkq
{

    /**
     * @brief Represents the implict vulkan queueFamily object
     * Used in the creation of command pools and queues.
     */
    class QueueFamily
    {
    public:
        QueueFamily() = default;
        ~QueueFamily() = default;

    public:
        /**
         * @brief Creates a queue family from a device and a queueFamilyIndex
         * 
         * @param device Parent device.
         * @param queueFamilyIndex Index of the queue family
         * @return QueueFamily 
         */
        static QueueFamily create(const Device& device, uint32_t queueFamilyIndex);

        /**
         * @brief Resets the queue family handle
         * 
         */
        void reset();

        /**
         * @brief Get the Surface Support K H R object
         * 
         * @param surface 
         * @return vk::Bool32 
         */
        vk::Bool32 getSurfaceSupportKHR(vk::SurfaceKHR surface);

        /**
         * @brief Gets the parent device of this queue family
         * 
         * @return Parent device of the queue family
         */
        Device device() const
        {
            return device_;
        }

        /**
         * @brief Gets the queueFamilyIndex of this queue family
         * 
         * @return The queueFamilyIndex that represents the implicit queue family object
         */
        uint32_t queueFamilyIndex() const
        {
            return queueFamilyIndex_;
        }

    private:
        explicit QueueFamily(Device device, uint32_t queueFamilyIndex);

        Device device_;
        uint32_t queueFamilyIndex_;
    };

    /**
     * @brief Represents a vk::Queue object
     * 
     */
    class Queue
    {
    public:
        Queue() = default;
        ~Queue() = default;

    public:
        /**
         * @brief Retrieves a vk::Queue handle from device, given a specific queue family index and queue index.
         * This functions has all the restrictions of vkGetDeviceQueue() (ie, no flags must have been set in vk::DeviceQueueCreateInfo).
         * 
         * @param device parent device of the queue
         * @param queueFamilyIndex index of queue family to retrieve queue from
         * @param queueIndex index of queue within queue family
         * @return Newly retrieved queue handle.
         */
        static Queue create(const Device& device, uint32_t queueFamilyIndex, uint32_t queueIndex);

        /**
         * @brief Retrieve a queue handle from a queue family.
         * This functions has all the restrictions of vkGetDeviceQueue() (ie, no flags must have been set in vk::DeviceQueueCreateInfo).
         * 
         * @param family Family to which the queue belongs.
         * @param queueIndex Index of the queue.
         * @return Newly retrieved queue handle.
         */
        static Queue create(const QueueFamily& family, uint32_t queueIndex)
        {
            return create(family.device(), family.queueFamilyIndex(), queueIndex);
        }

#ifdef VK_VERSION_1_1

        /**
         * @brief Retrieve a queue that was created with specific flags via vkGetDeviceQueue2().
         * This function is vulkan version 1.1 and above only.
         * 
         * @param device Parent device to which the queue belongs
         * @param queueInfo Info used to retrieve the queue handle
         * @return Newly retrieved queue handle. 
         */
        static Queue create2(const Device& device, const vk::DeviceQueueInfo2& queueInfo);

        /**
         * @brief Retrueve a queue via vkGetDeviceQueue2()
         * This function is vulkan version 1.1 and above only.
         * 
         * @param family Queue Family the queue belongs to
         * @param queueIndex Index of the retrieved queue
         * @param flags Flags used to originally create the queue in vk::DeviceQueueCreateInfo
         * @param pNext Pointer to extend the vk::DeviceQueueInfo2 structure
         * @return Queue 
         */
        static Queue create2(const QueueFamily& family, uint32_t queueIndex, vk::DeviceQueueCreateFlags flags = {}, const VkNextProxy<vk::DeviceQueueInfo2>& next = {})
        {
            vk::DeviceQueueInfo2 queueInfo{};
            queueInfo.pNext = next;
            queueInfo.flags = flags;
            queueInfo.queueFamilyIndex = family.queueFamilyIndex();
            queueInfo.queueIndex = queueIndex;

            return create2(family.device(), queueInfo);
        }
#endif

        /**
         * @brief Resets this handle
         */
        void reset();

#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME

        void beginDebugUtilsLabelEXT(const vk::DebugUtilsLabelEXT& labelInfo)
        {
            queue_.beginDebugUtilsLabelEXT(labelInfo, device_.dispatch());
        }

        void endDebugUtilsLabelEXT()
        {
            queue_.endDebugUtilsLabelEXT(device_.dispatch());
        }

        void insertDebugUtilsLabelEXT(const vk::DebugUtilsLabelEXT& labelInfo)
        {
            queue_.insertDebugUtilsLabelEXT(labelInfo, device_.dispatch());
        }

#endif

#ifdef VK_KHR_SWAPCHAIN_EXTENSION_NAME

        vk::Result presentKHR(const vk::PresentInfoKHR presentInfo)
        {
            return queue_.presentKHR(presentInfo, device_.dispatch());
        }

#endif

        void bindSparse(vk::ArrayProxy<const vk::BindSparseInfo> const& bindInfo, vk::Fence fence)
        {
            queue_.bindSparse(bindInfo, fence, device_.dispatch());
        }

        void submit(vk::ArrayProxy<const vk::SubmitInfo> const& submits, vk::Fence fence)
        {
            queue_.submit(submits, fence, device_.dispatch());
        }

        void waitIdle()
        {
            queue_.waitIdle(device_.dispatch());
        }

        Device device() const { return device_; }
        const vk::DispatchLoaderDynamic& dispatch() const { return device_.dispatch(); }

        vk::Queue vkQueue() const { return queue_; }
        vk::Queue vkHandle() const { return queue_; }
        operator vk::Queue() const { return queue_; }

    private:
        explicit Queue(Device device_, vk::Queue queue_);

        Device device_;
        vk::Queue queue_;
    };

} // namespace vkq
