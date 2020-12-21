#include "debugFactory.hpp"

namespace vkq
{
    
    DebugUtilsMessengerEXTFactory::DebugUtilsMessengerEXTFactory(Instance instance)
        : instance(instance)
    {
    }

     DebugUtilsMessengerEXT DebugUtilsMessengerEXTFactory::build()
     {
         vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
         createInfo.messageSeverity = severityFlags;
         createInfo.messageType = typeFlags;
         createInfo.pUserData = nullptr;
         createInfo.pfnUserCallback = messengerCallback;

         return instance.createDebugUtilsMessengerEXT(createInfo);
     }
}