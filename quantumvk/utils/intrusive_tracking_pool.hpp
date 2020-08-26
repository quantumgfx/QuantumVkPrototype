#pragma once

#include "object_pool.hpp"

#include <vector>

namespace Util
{

	class IntrusiveTrackingEnabled
	{

	public:

		uint32_t GetIndex()
		{
			return index;
		}

		void SetIndex(uint32_t index_)
		{
			index = index_
		}


	private:

		uint64_t index = 0;

	};

	template<typename T>
	class IntrusiveObjectPool
	{

	};


}