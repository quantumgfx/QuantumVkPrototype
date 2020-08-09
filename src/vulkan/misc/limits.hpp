#pragma once

/**
*Contains the minimum requirments for various settings in vulkan
*/

namespace Vulkan
{
	constexpr unsigned VULKAN_NUM_DESCRIPTOR_SETS = 8;
	constexpr unsigned VULKAN_NUM_BINDINGS = 16;
	constexpr unsigned VULKAN_NUM_BINDINGS_BINDLESS_VARYING = 64 * 1024;
	constexpr unsigned VULKAN_NUM_BINDINGS_BINDLESS = 4 * 1024;
	constexpr unsigned VULKAN_NUM_ATTACHMENTS = 8;
	constexpr unsigned VULKAN_NUM_VERTEX_ATTRIBS = 16;
	constexpr unsigned VULKAN_NUM_VERTEX_BUFFERS = 4;
	constexpr unsigned VULKAN_PUSH_CONSTANT_SIZE = 128;
	constexpr unsigned VULKAN_MAX_UBO_SIZE = 16 * 1024;
	constexpr unsigned VULKAN_NUM_SPEC_CONSTANTS = 8;
}
