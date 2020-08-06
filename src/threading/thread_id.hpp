#pragma once

//Contains a single thread id

namespace Vulkan
{
	unsigned get_current_thread_index();
	void register_thread_index(unsigned thread_index);
}