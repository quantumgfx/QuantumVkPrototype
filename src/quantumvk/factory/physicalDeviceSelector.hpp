#pragma once

#include "../core/instance.hpp"
#include "../core/physicalDevice.hpp"
#include "../core/vk.hpp"

namespace vkq
{

    /**
	 * @brief Helper class to select appopriate vk::PhysicalDevice based on certain criteria.
	 * 
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

        //////////////////////////////////
        // Misc //////////////////////////
        //////////////////////////////////

#ifdef VK_KHR_SURFACE_EXTENSION_NAME

        PhysicalDeviceSelector& setSurfaceKHR(vk::SurfaceKHR surface);
#endif
        /////////////////////////////////
        // Select ///////////////////////
        /////////////////////////////////

        vk::PhysicalDevice select();

    private:
        Instance instance;

        uint32_t minimumVersion = VK_MAKE_VERSION(1, 0, 0);

        std::vector<const char*> requiredExtensions = {};
        std::vector<vk::PhysicalDeviceType> allowedTypes = {vk::PhysicalDeviceType::eIntegratedGpu, vk::PhysicalDeviceType::eDiscreteGpu, vk::PhysicalDeviceType::eVirtualGpu, vk::PhysicalDeviceType::eCpu, vk::PhysicalDeviceType::eOther};
    };

} // namespace vkq