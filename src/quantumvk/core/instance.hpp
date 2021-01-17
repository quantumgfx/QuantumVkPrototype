#pragma once

#include "../base/vk.hpp"
#include "loader.hpp"

namespace vkq
{

    //////////////////////////////
    // Instance //////////////////
    //////////////////////////////

    /**
     * @brief Opaque handle for vk::Instance. Manages and holds the instance, all instance level function pointers, 
     * as well as some additional functionality (like querying enabled layers and extensions and the like).
     */
    class Instance
    {
    public:
        struct ExtensionSupport
        {
#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME
            bool debugUtilsEXT = false;
#endif
#ifdef VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
            bool getPhysicalDeviceProperties2KHR = false;
#endif
#ifdef VK_KHR_SURFACE_EXTENSION_NAME
            bool surfaceKHR = false;
#endif
        };

    public:
        Instance() = default;
        ~Instance() = default;

    public:
        /**
         * @brief Creates a new instance object from function pointer to vkGetInstanceProcAddr and given createInfo
         * 
         * @param getInstanceProcAddr PFN_vkGetInstanceProcAddr retrieved from vulkan dynamic loader library.
         * @param createInfo Info that is passed into vk::createInstance() to create the vk::Instance.
         * @return New Instance Handle  
         */
        static Instance create(PFN_vkGetInstanceProcAddr getInstanceProcAddr, const vk::InstanceCreateInfo& createInfo);

        static Instance create(const Loader& loader, const vk::InstanceCreateInfo& createInfo) { return create(loader.instanceProcAddrLoader(), createInfo); }

        /**
         * @brief Destroys instance and frees memory allocated by this object.
         * All created child objects must have been destroyed.
         */
        void destroy();

#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME

        vk::DebugUtilsMessengerEXT createDebugUtilsMessengerEXT(const vk::DebugUtilsMessengerCreateInfoEXT& createInfo, vk::Optional<const vk::AllocationCallbacks> allocator = nullptr);
        void destroyDebugUtilsMessengerEXT(vk::DebugUtilsMessengerEXT messenger, vk::Optional<const vk::AllocationCallbacks> allocator = nullptr);

#endif

        std::vector<vk::PhysicalDevice> enumeratePhysicalDevices() const;

        /**
         * @brief Checks whether a certain extension was enabled when the instance was created.
         * 
         * @param extensionName Name of the extension
         * @return True if the extension is enabled. False otherwise.
         */
        bool isInstanceExtensionEnabled(const char* extensionName) const;

        /**
         * @brief Checks whether a certain layer was enabled when the instance was created.
         * 
         * @param layerName Name of the layer
         * @return True if the layer is enabled. False otherwise.
         */
        bool isLayerEnabled(const char* layerName) const;

        /**
         * @brief Retrieves the vk::ApplicationInfo used to create the instance handle.
         * 
         * @return vk::ApplicationInfo used to create the instance handle 
         */
        const vk::ApplicationInfo& applicationInfo() const;

        /**
         * @brief Retrieves the uint32_t used to create the instance handle
         * 
         * @return uint32_t specifing the instance version.
         */
        uint32_t apiVersion() const { return applicationInfo().apiVersion; }

        /**
         * @brief Retrieves information on whether certain important extensions are supported
         * 
         * @return Struct containing bools indicating support for certain important instance extensions.
         */
        const ExtensionSupport& extensionSupport() const;

        /**
         * @brief Gets dynamic dispatcher suitable to call any instance or device level functions.
         * Note: it is slightly more efficient to use a device dispatch to call device level functions.
         * 
         * @return vk::DispatchLoaderDynamic with all available function pointers set.
         */
        const vk::DispatchLoaderDynamic& dispatch() const;

        /**
         * @brief Get the PFN_vkGetInstanceProcAddr that represents the implcit vulkan loader.
         * 
         * @return PFN_vkGetInstanceProcAddr pfn used to load all global and instance level functions.
         */
        PFN_vkGetInstanceProcAddr instanceProcAddrLoader() const;

        vk::Instance vkInstance() const;
        vk::Instance vkHandle() const;
        operator vk::Instance() const;

    private:
        struct Impl
        {
            vk::Instance instance;
            vk::DispatchLoaderDynamic dispatch = {};

            vk::ApplicationInfo appInfo = {};
            std::vector<const char*> enabledLayers;
            std::vector<const char*> enabledExtensions;

            Instance::ExtensionSupport extensionSupport;
        };

        explicit Instance(Impl* impl);

        Impl* impl_ = nullptr;
    };

} // namespace vkq