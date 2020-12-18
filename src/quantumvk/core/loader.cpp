#include "loader.hpp"

namespace vkq
{

	/////////////////////////////
	// Loader ///////////////////
	/////////////////////////////

	Loader Loader::create(PFN_vkGetInstanceProcAddr getInstanceProcAddr)
	{
		Loader loader{ new Loader::VkqType() };

		loader.type->dispatch.init(getInstanceProcAddr);

		return loader;
	}

	void Loader::destroy()
	{
		delete type;
		type = nullptr;
	}

	uint32_t Loader::enumerateInstanceVersion() const
	{
#ifdef VK_VERSION_1_1
		return vk::enumerateInstanceVersion(type->dispatch);
#endif
		return VK_MAKE_VERSION(1, 0, 0);
	}

	std::vector<vk::LayerProperties> Loader::enumerateInstanceLayerProperties() const
	{
		return vk::enumerateInstanceLayerProperties(type->dispatch);
	}

	std::vector<vk::ExtensionProperties> Loader::enumerateInstanceExtensionProperties() const
	{
		return vk::enumerateInstanceExtensionProperties(nullptr, type->dispatch);
	}

	std::vector<vk::ExtensionProperties> Loader::enumerateInstanceExtensionProperties(const std::string& layerName) const
	{
		return vk::enumerateInstanceExtensionProperties(layerName, type->dispatch);
	}

	vk::Instance Loader::createInstance(const vk::InstanceCreateInfo& createInfo, vk::Optional<const vk::AllocationCallbacks> allocator) const
	{
		return vk::createInstance(createInfo, allocator, type->dispatch);
	}

}