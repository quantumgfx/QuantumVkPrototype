#pragma once

//Contains a single thread id

namespace Vulkan
{
	unsigned GetCurrentThreadIndex();
	void register_thread_index(unsigned thread_index);
}