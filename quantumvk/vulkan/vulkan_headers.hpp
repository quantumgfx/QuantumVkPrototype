#pragma once

// NOTE: The Vulkan symbols are function pointers and are provided by the "volk" project.
#include "quantumvk/extern_build/volk_include.hpp"

#include "quantumvk/utils/logging.hpp"

#include <cstdlib>
#include <stdexcept>

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
// Workaround silly Xlib headers that define macros for these globally :(
#undef None
#undef Bool
#endif

#ifdef VULKAN_DEBUG
#define VK_ASSERT(x) if(!(x)) { QM_LOG_ERROR("Asertion Failed at %s:%d", __FILE__, __LINE__); abort(); }

#else
#define VK_ASSERT(x) ((void)0)
#endif

#define QM_NO_MOVE_NO_COPY(type) \
void operator=(const type##&) = delete; \
type##(const type##&) = delete;


namespace Vulkan
{
	struct NoCopyNoMove
	{
		NoCopyNoMove() = default;
		NoCopyNoMove(const NoCopyNoMove&) = delete;
		void operator=(const NoCopyNoMove&) = delete;
	};
}