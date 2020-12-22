#pragma once

#include "../core/vk.hpp"
#include "../core/instance.hpp"
#include "../core/physicalDevice.hpp"

namespace vkq {

	class PhysicalDeviceSelector {
		
		public:

			explicit PhysicalDeviceSelector(Instance instance);
			~PhysicalDeviceSelector();

			// Prefer a physical device that supports a version of vulkan.
			PhysicalDeviceSelector& setDesiredVersion(uint32_t version);
			// Require a physical device that supports a (major, minor) version of vulkan.
			PhysicalDeviceSelector& setMinimumVersion(uint32_t version);
			// Prefer a physical device that supports a (major, minor) version of vulkan.
			PhysicalDeviceSelector& setDesiredVersion(uint32_t major, uint32_t minor) { return setDesiredVersion(VK_MAKE_VERSION(major, minor, 0)); }
			// Require a physical device that supports a (major, minor) version of vulkan.
			PhysicalDeviceSelector& setMinimumVersion(uint32_t major, uint32_t minor) { return setMinimumVersion(VK_MAKE_VERSION(major, minor, 0)); }

		private:

			Instance instance;

			uint32_t desiredVersion = VK_MAKE_VERSION(1, 0, 0);
			uint32_t minimumVersion = VK_MAKE_VERSION(1, 0, 0);

			
	};

} // namespace vkq