#pragma once

#include "../base/vk.hpp"
#include "../core/instance.hpp"
#include "../core/physical_device.hpp"

namespace vkq
{

    /**
     * @brief Helper class to select appopriate vk::PhysicalDevice based on certain criteria.
     * These criteria can be of two types. One, you can specify a list of requirenments
     * (via setMinumumVersion(), requireExtension(), etc.) that any selected device must 
     * satisfy. IF none do, select() throws a runtime error. The remaining devices can be 
     * sorted by a weight score for each requested feature (setDesiredVersion(), etc.). 
     */
    class PhysicalDeviceSelector
    {

    public:
        /**
	* @brief Construct a new Physical Device Selector. 
	* 
        * @param instance Instance must be referenced to obtain a list of available 
	* physical devices as well as the dynamic dispatcher.
	*/
        explicit PhysicalDeviceSelector(Instance instance);

#ifdef VK_KHR_SURFACE_EXTENSION_NAME

        /**
         * @brief Create a Physical Device Selector using an Instance and a SurfaceKHR
         * 
         * @param instance Instance must be referenced to obtain a list of available physical devices as well as the dynamic dispatcher.
         * @param surface SurfaceKHR that is present support is queried with.
         */
        explicit PhysicalDeviceSelector(Instance instance, vk::SurfaceKHR surface);

#endif

        /**
	* @brief Default destructor of a Physical Device Selector
	* 
	*/
        ~PhysicalDeviceSelector();

        /////////////////////////////////////////////////////////
        // Set requirements for a device even to be considered //
        /////////////////////////////////////////////////////////

        /**
	* @brief Set minimum version of vulkan the Physical Device Selected must support. Defaults to 1.0.0.
	* 
	* @param version Minumum version of Vulkan
	* @return Reference to the selector object
	*/
        PhysicalDeviceSelector& setMinimumVersion(uint32_t version);

        /**
	* @brief Set minimum (major, minor) version of vulkan the Physical Device Selected must support.  Defaults to 1.0.0.
	* 
	* @param major Major version of Vulkan. Currently only major version: 1 is supported
	* @param minor Minor version of Vulkan. Currently only minor versions: 0, 1, 2 is supported
	* @return Reference to the selector object
	*/
        PhysicalDeviceSelector& setMinimumVersion(uint32_t major, uint32_t minor) { return setMinimumVersion(VK_MAKE_VERSION(major, minor, 0)); }

        /**
         * @brief Add extension that the physical device must support.
         * 
         * @param extensionName name of extension
         * @return Reference to the selector object
         */
        PhysicalDeviceSelector& requireExtension(const char* extensionName);

        /**
         * @brief Adds set of extensions that the physical device must support.
         * 
         * @param extensionNames names of extensions
         * @return Reference to the selector object
         */
        PhysicalDeviceSelector& requireExtensions(const std::vector<const char*>& extensionNames);

        /**
         * @brief Sets the allowed physical device types. Defaults to all types.
         * 
         * @param types The set of allowed physical device types.
         * @return Reference to the selector object
         */
        PhysicalDeviceSelector& setAllowedPhysicalDeviceTypes(const std::vector<vk::PhysicalDeviceType>& types);

#ifdef VK_KHR_SURFACE_EXTENSION_NAME
        /**
         * @brief Sets the surface that will be used to query physical device presentation support.
         * 
         * @param surface SurfaceKHR that the physical device will use to query presentation support.
         * @return Reference to the selector object
         */
        PhysicalDeviceSelector& setSurfaceKHR(vk::SurfaceKHR surface);

        /**
         * @brief Sets whether at least one of the physical device's queues must be able to present to surface. 
         * Defaults to false.
         * 
         * @param support Set to true if surface presentation must be supported by at least one queue family. False otherwise.
         * @return Reference to the selector object
         */
        PhysicalDeviceSelector& setSupportSurfaceKHR(bool support);
#endif

        PhysicalDeviceSelector& setDesiredVersion(float weight, uint32_t version);

        PhysicalDeviceSelector& setDesiredVersion(float weight, uint32_t major, uint32_t minor) { return setDesiredVersion(weight, VK_MAKE_VERSION(major, minor, 0)); }

        PhysicalDeviceSelector& requestExtension(float weight, const char* extensionName);

        PhysicalDeviceSelector& requestExtensions(float weight, const std::vector<const char*>& extensionNames);

        PhysicalDeviceSelector& preferPhysicalDeviceType(float weight, vk::PhysicalDeviceType type);

        // PhysicalDeviceSelector& requireQueueFamily(vk::QueueFlags requiredFlags, vk::QueueFlags excludedFlags, uint32_t queueCount);

        /////////////////////////////////
        // Select ///////////////////////
        /////////////////////////////////

        PhysicalDevice select();

    private:
        Instance instance;

        uint32_t minimumVersion = VK_MAKE_VERSION(1, 0, 0);

        std::vector<const char*> requiredExtensions = {};
        std::vector<vk::PhysicalDeviceType> allowedTypes = {vk::PhysicalDeviceType::eIntegratedGpu, vk::PhysicalDeviceType::eDiscreteGpu, vk::PhysicalDeviceType::eVirtualGpu, vk::PhysicalDeviceType::eCpu, vk::PhysicalDeviceType::eOther};

#ifdef VK_KHR_SURFACE_EXTENSION_NAME

        vk::SurfaceKHR presentSurface;
        bool presentSupportRequired = false;

#endif

        uint32_t desiredVersion = VK_MAKE_VERSION(1, 0, 0);
        float desiredVersionWeight = 0;

        std::vector<std::pair<const char*, float>> requestedExtensions;
        std::vector<std::pair<vk::PhysicalDeviceType, float>> typePreferences;
    };

} // namespace vkq