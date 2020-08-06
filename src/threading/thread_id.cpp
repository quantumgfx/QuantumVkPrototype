#include "thread_id.hpp"
#include "utils/logging.hpp"

namespace Vulkan
{
static thread_local unsigned thread_id_to_index = ~0u;

unsigned get_current_thread_index()
{
	auto ret = thread_id_to_index;
	if (ret == ~0u)
	{
		QM_LOG_WARN("Thread does not exist in thread manager or is not the main thread.\n");
		return 0;
	}
	return ret;
}

void register_thread_index(unsigned index)
{
	thread_id_to_index = index;
}
}
