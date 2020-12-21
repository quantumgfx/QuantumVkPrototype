#pragma once

#include "../headers/vk.hpp"
#include "loader.hpp"

namespace vkq
{

#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME

	class DebugUtilsMessengerEXT
	{
	public:

		DebugUtilsMessengerEXT(const DebugUtilsMessengerEXT&) = default;

		DebugUtilsMessengerEXT(vk::DebugUtilsMessengerEXT messenger)
		: messenger(messenger)
		{
		}

		~DebugUtilsMessengerEXT() = default;

		DebugUtilsMessengerEXT& operator =(const DebugUtilsMessengerEXT&) = default;
		DebugUtilsMessengerEXT& operator =(vk::DebugUtilsMessengerEXT messenger_) 
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

	class SurfaceKHR
	{
	public:

		SurfaceKHR(const SurfaceKHR&) = default;
		SurfaceKHR(vk::SurfaceKHR surface)
			: surface(surface)
		{
		}

		~SurfaceKHR() = default;

		SurfaceKHR& operator =(const SurfaceKHR&) = default;
		SurfaceKHR& operator =(vk::SurfaceKHR surface_) 
		{ 
			surface = surface_; 
			return *this; 
		}

		vk::SurfaceKHR vkSurface() const { return surface; }
		vk::SurfaceKHR vkHandle() const { return surface; }

		operator vk::SurfaceKHR() const { return surface; }

	private:

		vk::SurfaceKHR surface;

	};

#endif

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

#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME

		DebugUtilsMessengerEXT createDebugUtilsMessengerEXT(const vk::DebugUtilsMessengerCreateInfoEXT& createInfo, vk::Optional<const vk::AllocationCallbacks> allocator = nullptr);
		void destroyDebugUtilsMessengerEXT(DebugUtilsMessengerEXT messenger, vk::Optional<const vk::AllocationCallbacks> allocator = nullptr);
		
#endif

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

}