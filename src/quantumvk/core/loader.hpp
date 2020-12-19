#pragma once

#include "../headers/vk.hpp"

namespace vkq
{
	/**
	 * @brief Object representing how the vulkan library is linked to the application. Loads global function pointers either 
	 * from dynamic or static loader (via PFN_vkGetInstanceProcAddr).
	*/
	class Loader
	{

		struct VkqType
		{
			vk::DispatchLoaderDynamic dispatch{};
		};

	public:

		Loader() = default;
		~Loader() = default;

	public:

		static Loader create(PFN_vkGetInstanceProcAddr getInstanceProcAddr);

		void destroy();

		PFN_vkGetInstanceProcAddr getInstanceProcAddrLoader() const { return type->dispatch.vkGetInstanceProcAddr; }
		const vk::DispatchLoaderDynamic& getGlobalDispatch() const { return type->dispatch; }

		uint32_t enumerateInstanceVersion() const;
		std::vector<vk::LayerProperties> enumerateInstanceLayerProperties() const;
		std::vector<vk::ExtensionProperties> enumerateInstanceExtensionProperties() const;
		std::vector<vk::ExtensionProperties> enumerateInstanceExtensionProperties(const char* layerName) const;
		vk::Instance createInstance(const vk::InstanceCreateInfo& create_info, vk::Optional<const vk::AllocationCallbacks> allocator = nullptr) const;

	private:

		Loader(VkqType* type)
			: type(type)
		{
		}

		VkqType* type = nullptr;

	};
}