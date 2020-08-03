#pragma once

// NOTE: The Vulkan symbols are function pointers and are provided by the "volk" project.

#include "lib/volk/volk.h"
#include <cstdlib>
#include <stdexcept>

namespace Vulkan
{
	struct NoCopyNoMove
	{
		NoCopyNoMove() = default;
		NoCopyNoMove(const NoCopyNoMove&) = delete;
		void operator=(const NoCopyNoMove&) = delete;
	};
}