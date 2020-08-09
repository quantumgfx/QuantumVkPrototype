#pragma once

#include <stdint.h>
#include "utils/hash.hpp"
#include "utils/intrusive_hash_map.hpp"

namespace Vulkan
{
	class Device;

	//Allows Object to be hashed/comparied easily I think
	class Cookie
	{
	public:
		Cookie(Device* device);

		uint64_t GetCookie() const
		{
			return cookie;
		}

	private:
		uint64_t cookie;
	};

	template <typename T>
	using HashedObject = Util::IntrusiveHashMapEnabled<T>;

	class InternalSyncEnabled
	{
	public:
		void SetInternalSyncObject()
		{
			internal_sync = true;
		}

	protected:
		bool internal_sync = false;
	};
}
