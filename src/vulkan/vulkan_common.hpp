#pragma once

#include "utils/intrusive.hpp"

#include "utils/intrusive.hpp"
#include "utils/object_pool.hpp"
#include "utils/intrusive_object_pool.hpp"
#include "utils/intrusive_hash_map.hpp"

namespace Vulkan
{
#ifdef QM_VULKAN_MT
	using HandleCounter = Util::MultiThreadCounter;
#else
	using HandleCounter = Util::SingleThreadCounter;
#endif

#ifdef QM_VULKAN_MT
	template <typename T>
	using VulkanObjectPool = Util::ThreadSafeObjectPool<T>;
	template <typename T>
	using VulkanIntrusiveObjectPool = Util::ThreadSafeIntrusiveObjectPool<T>;
	template <typename T>
	using VulkanCache = Util::ThreadSafeIntrusiveHashMap<T>;
#else
	template <typename T>
	using VulkanObjectPool = Util::ObjectPool<T>;
	template <typename T>
	using VulkanIntrusiveObjectPool = Util::IntrusiveObjectPool<T>;
	template <typename T>
	using VulkanCache = Util::IntrusiveHashMap<T>;
#endif
}