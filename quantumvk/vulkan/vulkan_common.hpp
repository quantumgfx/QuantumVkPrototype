#pragma once

#include "quantumvk/utils/intrusive.hpp"

#include "quantumvk/utils/intrusive.hpp"
#include "quantumvk/utils/object_pool.hpp"
#include "quantumvk/utils/intrusive_object_pool.hpp"
#include "quantumvk/utils/intrusive_hash_map.hpp"
#include "quantumvk/utils/retained_alloc.hpp"

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

	using VulkanDynamicArrayPool = Util::ThreadSafeDynamicArrayPool;
#else
	template <typename T>
	using VulkanObjectPool = Util::ObjectPool<T>;
	template <typename T>
	using VulkanIntrusiveObjectPool = Util::IntrusiveObjectPool<T>;
	template <typename T>
	using VulkanCache = Util::IntrusiveHashMap<T>;

	using VulkanDynamicArrayPool = Util::DynamicArrayPool;
#endif
}