#pragma once

#include "vulkan_headers.hpp"
#include <unordered_set>
#include <vector>

namespace Vulkan
{
	class Device;
	class CommandPool
	{
	public:

		CommandPool(Device* device, uint32_t queue_family_index);
		~CommandPool();

		CommandPool(CommandPool&&) noexcept;
		CommandPool& operator=(CommandPool&&) noexcept;
		CommandPool(const CommandPool&) = delete;
		void operator=(const CommandPool&) = delete;

		// Begin a new frame. Reset all command buffers belonging to pool.
		void Begin();
		// Returns a new primary command buffer allocated from and belonging to pool.
		VkCommandBuffer RequestCommandBuffer();
		// Returns a new secondary command buffer allocated from and belonging to pool.
		VkCommandBuffer RequestSecondaryCommandBuffer();
		// Signal that the command bffer has been submitted (debug only function).
		void SignalSubmitted(VkCommandBuffer cmd);

	private:
		Device* device;
		const VolkDeviceTable* table;
		VkCommandPool pool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> buffers;
		std::vector<VkCommandBuffer> secondary_buffers;
#ifdef VULKAN_DEBUG
		std::unordered_set<VkCommandBuffer> in_flight;
#endif
		unsigned index = 0;
		unsigned secondary_index = 0;
	};
}