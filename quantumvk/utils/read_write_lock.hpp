#pragma once

#include <atomic>

#ifdef __SSE2__
	#include <emmintrin.h>
#endif

namespace Util
{
	//Simple spin lock iplmenetation when writing it waits for all reads on other threads to finish
	class RWSpinLock
	{
	public:
		enum { Reader = 2, Writer = 1 };
		RWSpinLock()
		{
			counter.store(0);
		}

		inline void lock_read()
		{
			unsigned v = counter.fetch_add(Reader, std::memory_order_acquire);
			while ((v & Writer) != 0)
			{
#ifdef __SSE2__
				_mm_pause();
#endif
				v = counter.load(std::memory_order_acquire);
			}
		}

		inline void unlock_read()
		{
			counter.fetch_sub(Reader, std::memory_order_release);
		}

		inline void lock_write()
		{
			uint32_t expected = 0;
			while (!counter.compare_exchange_weak(expected, Writer,
				std::memory_order_acquire,
				std::memory_order_relaxed))
			{
#ifdef __SSE2__
				_mm_pause();
#endif
				expected = 0;
			}
		}

		inline void unlock_write()
		{
			counter.fetch_and(~Writer, std::memory_order_release);
		}

		inline void promote_reader_to_writer()
		{
			uint32_t expected = Reader;
			if (!counter.compare_exchange_strong(expected, Writer,
				std::memory_order_acquire,
				std::memory_order_relaxed))
			{
				unlock_read();
				lock_write();
			}
		}

	private:
		std::atomic<uint32_t> counter;
	};
}
