#include "cookie.hpp"
#include "quantumvk/vulkan/device.hpp"

namespace Vulkan
{
    Cookie::Cookie(Device* device)
        : cookie(device->AllocateCookie())
    {
    }
}