#pragma once

#include "../base/vk.hpp"
#include "instance.hpp"
#include "loader.hpp"
#include "physical_device.hpp"

namespace vkq
{

    /**
     * @brief A handle representing a vk::Device and a dynamic dispatcher
     * 
     */
    class Device
    {
    public:
        struct ExtensionSupport
        {
#ifdef VK_KHR_SWAPCHAIN_EXTENSION_NAME
            bool swapchainKHR = false;
#endif
        };

    private:
        struct Impl
        {
            vk::Instance instance;
            vk::PhysicalDevice phdev;
            vk::Device device;
            vk::DispatchLoaderDynamic dispatch;

            std::vector<const char*> enabledExtensions;

            Device::ExtensionSupport extensionSupport;

            uint32_t deviceVersion = VK_MAKE_VERSION(1, 0, 0);
        };

    public:
        Device() = default;
        ~Device() = default;

    public:
        /**
         * @brief Creates a new device given native vk:: handles and createInfo.
         * 
         * @param getInstanceProcAddr PFN_vkGetInstanceProcAddr that connects the application to the vulkan loader
         * @param instance Vulkan instance
         * @param phdev Physical Device that the logical device will interface with.
         * @param createInfo Info to be passed into vk:: createDevice() that creates the vk::Device handle
         * @return Newly created device
         */
        static Device create(PFN_vkGetInstanceProcAddr getInstanceProcAddr, vk::Instance instance, vk::PhysicalDevice phdev, const vk::DeviceCreateInfo& createInfo);

        /**
         * @brief Creates a new device given a vkq::Loader and a series of vk:: handles and createInfo.
         * 
         * @param loader vkq::Loader object
         * @param instance Vulkan instance
         * @param phdev Physical Device that the logical device will interface with.
         * @param createInfo Info to be passed into vk:: createDevice() that creates the vk::Device handle
         * @return Newly created device 
         */
        static Device create(const Loader& loader, vk::Instance instance, vk::PhysicalDevice phdev, const vk::DeviceCreateInfo& createInfo) { return create(loader.getInstanceProcAddrLoader(), instance, phdev, createInfo); }

        /**
         * @brief Creates a new device given a vkq::Instance, a vk::PhysicalDevice, and vk::DeviceCreateInfo/
         * 
         * @param instance Parent instance of device
         * @param phdev Physical Device that the logical device will interface with.
         * @param createInfo Info to be passed into vk:: createDevice() that creates the vk::Device handle
         * @return Newly created device 
         */
        static Device create(const Instance& instance, vk::PhysicalDevice phdev, const vk::DeviceCreateInfo& createInfo) { return create(instance.getInstanceProcAddrLoader(), instance.vkInstance(), phdev, createInfo); }

        /**
         * @brief Creates a new device given a vkq::PhysicalDevice, and vk::DeviceCreateInfo.
         * 
         * @param phdev Physical Device that the logical device will interface with.
         * @param createInfo Info to be passed into vk:: createDevice() that creates the vk::Device handle
         * @return Newly created device 
         */
        static Device create(const PhysicalDevice& phdev, const vk::DeviceCreateInfo& createInfo) { return create(phdev.getInstance(), phdev.vkPhysicalDevice(), createInfo); }

        /**
         * @brief Destroys device object and invalidates all references to it
         * 
         */
        void destroy();

        //////////////////////////////////
        // Core Alias functions //////////
        //////////////////////////////////

        std::vector<vk::CommandBuffer> allocateCommandBuffers(const vk::CommandBufferAllocateInfo& allocateInfo) const
        {
            return vkDevice().allocateCommandBuffers(allocateInfo, getDeviceDispatch());
        }

        vk::CommandPool createCommandPool(const vk::CommandPoolCreateInfo& createInfo) const
        {
            return vkDevice().createCommandPool(createInfo, nullptr, getDeviceDispatch());
        }

        void destroyCommandPool(vk::CommandPool commandPool) const
        {
            vkDevice().destroyCommandPool(commandPool, nullptr, getDeviceDispatch());
        }

        void freeCommandBuffers(vk::CommandPool commandPool, const vk::ArrayProxy<const vk::CommandBuffer>& commandBuffers) const
        {
            vkDevice().freeCommandBuffers(commandPool, commandBuffers, getDeviceDispatch());
        }

        vk::Queue getQueue(uint32_t queueFamilyIndex, uint32_t queueIndex) const
        {
            return vkDevice().getQueue(queueFamilyIndex, queueIndex, getDeviceDispatch());
        }

        void resetCommandPool(vk::CommandPool commandPool, vk::CommandPoolResetFlags flags = {}) const
        {
            vkDevice().resetCommandPool(commandPool, flags, getDeviceDispatch());
        }

        ////////////////////////////////
        // Version 1.1 /////////////////
        ////////////////////////////////

#ifdef VK_VERSION_1_1

        void trimCommandPool(vk::CommandPool commandPool, vk::CommandPoolTrimFlags flags = {}) const
        {
            vkDevice().trimCommandPool(commandPool, flags, getDeviceDispatch());
        }

        vk::Queue getQueue2(const vk::DeviceQueueInfo2& queueInfo) const
        {
            vkDevice().getQueue2(queueInfo, getDeviceDispatch());
        }

#endif

        ///////////////////////////////
        // Extenstions ////////////////
        ///////////////////////////////

#ifdef VK_KHR_MAINTENANCE1_EXTENSION_NAME

        void trimCommandPoolKHR(vk::CommandPool commandPool, vk::CommandPoolTrimFlagsKHR flags = {}) const
        {
            vkDevice().trimCommandPoolKHR(commandPool, flags, getDeviceDispatch());
        }

#endif

        ///////////////////////////////
        // Helper Functions ///////////
        ///////////////////////////////

        void allocateCommandBuffers(vk::CommandPool commandPool, uint32_t commandBufferCount, vk::CommandBuffer* commandBuffers, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next = {}) const
        {
            vk::CommandBufferAllocateInfo allocInfo{};
            allocInfo.pNext = next;
            allocInfo.commandPool = commandPool;
            allocInfo.level = level;
            allocInfo.commandBufferCount = commandBufferCount;

            vk::Result result = vkDevice().allocateCommandBuffers(&allocInfo, commandBuffers, getDeviceDispatch());

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

        bool isDeviceExtensionEnabled(const char* extensionName) const;

        const ExtensionSupport& getDeviceExtensionSupport() const;

        ///////////////////////////////
        // Native Objects /////////////
        ///////////////////////////////

        /**
         * @brief Get the device dispatcher
         * 
         * @return A vk::DispatchLoaderDynamic capable of running all global, instance, and device level functions
         * (associated with this device or the parent instance).
         */
        const vk::DispatchLoaderDynamic& getDeviceDispatch() const;

        /**
         * @brief Get the PFN_vkGetInstanceProcAddr used to load instance level function pointers.
         * 
         * @return The PFN_vkGetInstanceProcAddr used to load instance level function pointers.
         */
        PFN_vkGetInstanceProcAddr getInstanceProcAddrLoader() const { return getDeviceDispatch().vkGetInstanceProcAddr; }

        /**
         * @brief Get the PFN_vkGetDeviceProcAddr used to load device level function pointers.
         * 
         * @return The PFN_vkGetDeviceProcAddr used to load device level function pointers.
         */
        PFN_vkGetDeviceProcAddr getDeviceProcAddrLoader() const { return getDeviceDispatch().vkGetDeviceProcAddr; }

        vk::Instance vkInstance() const;
        vk::PhysicalDevice vkPhysicalDevice() const;
        vk::Device vkDevice() const;
        vk::Device vkHandle() const;
        operator vk::Device() const;

    private:
        explicit Device(Impl* impl)
            : impl(impl)
        {
        }

        Impl* impl = nullptr;
    };
} // namespace vkq