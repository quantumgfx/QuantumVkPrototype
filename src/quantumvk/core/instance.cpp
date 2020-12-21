#include "instance.hpp"

#include <cstring>

namespace vkq
{
    ////////////////////////////////
    // Instance ////////////////////
    ////////////////////////////////

    Instance Instance::create(Loader loader, const vk::InstanceCreateInfo& createInfo)
    {
        Instance instance{ new Instance::VkqType() };

        instance.type->dispatch = loader.getGlobalDispatch();
        instance.type->instance = vk::createInstance(createInfo, nullptr, instance.type->dispatch);
        instance.type->dispatch.init(instance.type->instance);

        return instance;
    }

    void Instance::destroy()
    {
        type->instance.destroy(nullptr, type->dispatch);

        delete type;
        type = nullptr;

    }

#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME

    DebugUtilsMessengerEXT Instance::createDebugUtilsMessengerEXT(const vk::DebugUtilsMessengerCreateInfoEXT& createInfo, vk::Optional<const vk::AllocationCallbacks> allocator)
    {
        return DebugUtilsMessengerEXT{ type->instance.createDebugUtilsMessengerEXT(createInfo, allocator, type->dispatch) };
    }

    void Instance::destroyDebugUtilsMessengerEXT(DebugUtilsMessengerEXT messenger, vk::Optional<const vk::AllocationCallbacks> allocator)
    {
        type->instance.destroyDebugUtilsMessengerEXT(static_cast<vk::DebugUtilsMessengerEXT>(messenger), allocator, type->dispatch);
    }

#endif

}