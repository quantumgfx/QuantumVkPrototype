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

		void Begin();
		VkCommandBuffer RequestCommandBuffer();
		VkCommandBuffer RequestSecondaryCommandBuffer();
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