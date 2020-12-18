#pragma once

#include "../headers/vk.hpp"
#include "loader.hpp"

namespace vkq
{

	class Instance
	{
		struct VkqType
		{
			vk::Instance instance;
			vk::DispatchLoaderDynamic dispatch;
		};

	public:

		Instance() = default;
		~Instance() = default;

	public:

		static Instance create(Loader loader, const vk::InstanceCreateInfo& createInfo);

		void destroy();

		const vk::DispatchLoaderDynamic& getInstanceDispatch() const { return type->dispatch; }

		vk::Instance vkInstance() const { return type->instance; }
		vk::Instance vkHandle() const { return type->instance; }
		operator vk::Instance() const { return type->instance; }

		explicit operator bool() const noexcept { return type != nullptr; }


	private:

		Instance(VkqType* type)
			: type(type)
		{
		}

		VkqType* type = nullptr;


	};

	class InstanceFactory
	{
	public:

		explicit InstanceFactory(Loader loader);
		~InstanceFactory() = default;

		
		InstanceFactory& requireApiVersion(uint32_t version);
		InstanceFactory& requestApiVersion(uint32_t version);
		InstanceFactory& requireApiVersion(uint32_t major, uint32_t minor, uint32_t patch = 0) { return requireApiVersion(VK_MAKE_VERSION(major, minor, patch)); }
		InstanceFactory& requestApiVersion(uint32_t major, uint32_t minor, uint32_t patch = 0) { return requestApiVersion(VK_MAKE_VERSION(major, minor, patch)); }

		InstanceFactory& setAppName(const char* appName) { appName_ = appName; return *this; }
		InstanceFactory& setEngineName(const char* engineName) { engineName_ = engineName; return *this; }
		InstanceFactory& setAppVersion(uint32_t version) { appVersion = version; return *this; }
		InstanceFactory& setEngineVersion(uint32_t version) { engineVersion = version; return *this; }
		InstanceFactory& setAppVersion(uint32_t major, uint32_t minor, uint32_t patch = 0) { return setAppVersion(VK_MAKE_VERSION(major, minor, patch)); }
		InstanceFactory& setEngineVersion(uint32_t major, uint32_t minor, uint32_t patch = 0) { return setEngineVersion(VK_MAKE_VERSION(major, minor, patch)); }

		InstanceFactory& enableLayer(const char* layerName);
		InstanceFactory& enableExtension(const char* extensionName);


	private:

		const char* appName_ = nullptr;
		const char* engineName_ = nullptr;
		uint32_t appVersion = 0;
		uint32_t engineVersion = 0;
		uint32_t requiredApiVersion = VK_MAKE_VERSION(1, 0, 0);
		uint32_t requestedApiVersion = VK_MAKE_VERSION(1, 0, 0);

		std::vector<const char*> layers;
		std::vector<const char*> extensions;

		Loader loader;

	};

}