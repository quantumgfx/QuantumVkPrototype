#pragma once

#include "quantumvk/vulkan/vulkan_headers.hpp"

#include <vector>
#include <cstddef>
#include <cassert>

//Various helpers for texture formats

namespace Vulkan
{
	class TextureFormatLayout
	{
	public:

		static uint32_t FormatBlockSize(VkFormat format, VkImageAspectFlags aspect);
		static void FormatBlockDim(VkFormat format, uint32_t& width, uint32_t& height);
		static uint32_t NumMiplevels(uint32_t width, uint32_t height = 1, uint32_t depth = 1);

	};
}
