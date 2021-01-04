#pragma once

#include "../base/vk.hpp"
#include "../core/instance.hpp"

namespace vkq
{

#ifdef VK_EXT_DEBUG_UTILS_EXTENSION_NAME

    class DebugUtilsMessengerEXTFactory
    {
    public:
        explicit DebugUtilsMessengerEXTFactory(Instance instance);
        ~DebugUtilsMessengerEXTFactory() = default;

        DebugUtilsMessengerEXTFactory& setSeverityFlags(vk::DebugUtilsMessageSeverityFlagsEXT flags)
        {
            severityFlags = flags;
            return *this;
        }
        DebugUtilsMessengerEXTFactory& setTypeFlags(vk::DebugUtilsMessageTypeFlagsEXT flags)
        {
            typeFlags = flags;
            return *this;
        }
        DebugUtilsMessengerEXTFactory& setCallback(PFN_vkDebugUtilsMessengerCallbackEXT callback)
        {
            messengerCallback = callback;
            return *this;
        }

        vk::DebugUtilsMessengerEXT build();

    private:
        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags;
        vk::DebugUtilsMessageTypeFlagsEXT typeFlags;
        PFN_vkDebugUtilsMessengerCallbackEXT messengerCallback;

        Instance instance;
    };

#endif
} // namespace vkq
