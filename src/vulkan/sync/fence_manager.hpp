#pragma once

#include "vulkan/vulkan_headers.hpp"
#include <vector>

namespace Vulkan
{
	class Device;
	// Class that manages use and reuse of fences
	// Just a linear allocator that stores every fence ever created
	// If you request a fence it either returns a reused fence
	// or creates a new one.
	class FenceManager
	{
	public:
		// Inits the fence manager
		void Init(Device* device);
		~FenceManager();

		// Returns a new or recycled fence
		VkFence RequestClearedFence();
		// Recycle an old unused fence. Deletes it once the fence manager is destroyed
		void RecycleFence(VkFence fence);

	private:
		Device* device = nullptr;
		const VolkDeviceTable* table = nullptr;
		std::vector<VkFence> fences;
	};
}
