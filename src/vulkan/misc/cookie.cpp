#include "vulkan/misc/cookie.hpp"
#include "vulkan/device.hpp"

namespace Vulkan
{
    Cookie::Cookie(Device* device)
        : cookie(device->AllocateCookie())
    {
    }
}