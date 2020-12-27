#pragma once

#include "../base/vk.hpp"
#include "physical_device.hpp"

namespace vkq
{

#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME

    //////////////////////////////
    // Debug Utils Messenger /////
    //////////////////////////////

    /**
     * @brief Simple transparent wrapper around vk::DebugUtilsMessengerEXT. 
     * Allows for automatic conversion.
     */
    class DebugUtilsMessengerEXT
    {
    public:
        DebugUtilsMessengerEXT() = default;
        DebugUtilsMessengerEXT(const DebugUtilsMessengerEXT&) = default;

        DebugUtilsMessengerEXT(vk::DebugUtilsMessengerEXT messenger)
            : messenger(messenger)
        {
        }

        ~DebugUtilsMessengerEXT() = default;

        DebugUtilsMessengerEXT& operator=(const DebugUtilsMessengerEXT&) = default;
        DebugUtilsMessengerEXT& operator=(vk::DebugUtilsMessengerEXT messenger_)
        {
            messenger = messenger_;
            return *this;
        }

        vk::DebugUtilsMessengerEXT vkMessenger() const { return messenger; }
        vk::DebugUtilsMessengerEXT vkHandle() const { return messenger; }

        operator vk::DebugUtilsMessengerEXT() const { return messenger; }

    private:
        vk::DebugUtilsMessengerEXT messenger;
    };

#endif

#ifdef VK_KHR_SURFACE_EXTENSION_NAME

    //////////////////////////////
    // Surface ///////////////////
    //////////////////////////////

    /**
     * @brief Simple transparent wrapper around vk::SurfaceKHR. 
     * Allows for automatic conversion.
     */
    class SurfaceKHR
    {
    public:
        SurfaceKHR() = default;
        SurfaceKHR(const SurfaceKHR&) = default;
        SurfaceKHR(vk::SurfaceKHR surface)
            : surface(surface)
        {
        }

        ~SurfaceKHR() = default;

        SurfaceKHR& operator=(const SurfaceKHR&) = default;
        SurfaceKHR& operator=(vk::SurfaceKHR surface_)
        {
            surface = surface_;
            return *this;
        }

        vk::SurfaceKHR vkSurface() const { return surface; }
        vk::SurfaceKHR vkHandle() const { return surface; }

        operator vk::SurfaceKHR() const { return surface; }

        explicit operator bool() const noexcept { return static_cast<bool>(surface); }

    private:
        vk::SurfaceKHR surface;
    };

#endif

    //////////////////////////////
    // Instance //////////////////
    //////////////////////////////

    struct InstanceImpl;

    /**
     * @brief Opaque handle for vk::Instance. Manages and holds the instance, all instance level function pointers, 
     * as well as some additional functionality (like querying enabled layers and extensions and the like).
     */
    class Instance
    {
    public:
        Instance() = default;
        ~Instance() = default;

        Instance(InstanceImpl* impl)
            : impl(impl)
        {
        }

    public:
        /**
         * @brief Creates a new instance object from function pointer to vkGetInstanceProcAddr and given createInfo
         * 
         * @param getInstanceProcAddr PFN_vkGetInstanceProcAddr retrieved from vulkan dynamic loader library.
         * @param createInfo Info that is passed into vk::createInstance() to create the vk::Instance.
         * @return New Instance Handle  
         */
        static Instance create(PFN_vkGetInstanceProcAddr getInstanceProcAddr, const vk::InstanceCreateInfo& createInfo);

        /**
         * @brief Destroys instance and frees memory allocated by this object.
         * All created child objects must have been destroyed.
         */
        void destroy();

        /**
         * @brief Enumerates through all physical devices associated with the instance. Identical to 
         * instance.enumeratePhysicalDevices();
         * 
         * @return Vector of physical devices associated with the instance.
         */
        std::vector<PhysicalDevice> enumeratePhysicalDevices() const;

#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME

        DebugUtilsMessengerEXT createDebugUtilsMessengerEXT(const vk::DebugUtilsMessengerCreateInfoEXT& createInfo, vk::Optional<const vk::AllocationCallbacks> allocator = nullptr);
        void destroyDebugUtilsMessengerEXT(DebugUtilsMessengerEXT messenger, vk::Optional<const vk::AllocationCallbacks> allocator = nullptr);

#endif
        /**
         * @brief Retrieves the vk::ApplicationInfo used to create instance handle.
         * 
         * @return vk::ApplicationInfo used to create instance handle 
         */
        const vk::ApplicationInfo& getApplicationInfo();

        /**
         * @brief Checks whether a certain extension was enabled when the instance was created.
         * 
         * @param extensionName Name of the extension
         * @return True if the extension is enabled. False otherwise.
         */
        bool isInstanceExtensionEnabled(const char* extensionName);

        /**
         * @brief Checks whether a certain layer was enabled when the instance was created.
         * 
         * @param layerName Name of the layer
         * @return True if the layer is enabled. False otherwise.
         */
        bool isLayerEnabled(const char* layerName);

        PFN_vkGetInstanceProcAddr getInstanceProcAddrLoader() const;

        /**
         * @brief Gets dynamic dispatcher suitable to call any instance or device level functions.
         * Note: it is slightly more efficient to use a device dispatch to call device level functions.
         * 
         * @return vk::DispatchLoaderDynamic with all available function pointers set.
         */
        const vk::DispatchLoaderDynamic& getInstanceDispatch() const;

        vk::Instance vkInstance() const;
        vk::Instance vkHandle() const;
        operator vk::Instance() const;

        InstanceImpl* getImpl() const { return impl; }

    private:
        InstanceImpl* impl = nullptr;
    };

} // namespace vkq