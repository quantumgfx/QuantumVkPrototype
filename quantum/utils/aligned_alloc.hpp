#pragma once

#include <stddef.h>
#include <stdexcept>
#include <new>

namespace Util
{
    //Allocs memory with an alignment.
    void* memalign_alloc(size_t boundary, size_t size);
    //Callocs memory with an alignment.
    void* memalign_calloc(size_t boundary, size_t size);
    //Frees memaligned memory
    void memalign_free(void* ptr);

    template <typename T>
    struct AlignedAllocation
    {
        static void* operator new(size_t size)
        {
            void* ret = ::Util::memalign_alloc(alignof(T), size);
            if (!ret) throw std::bad_alloc();
            return ret;
        }

        static void* operator new[](size_t size)
        {
            void* ret = ::Util::memalign_alloc(alignof(T), size);
            if (!ret) throw std::bad_alloc();
            return ret;
        }

            static void operator delete(void* ptr)
        {
            return ::Util::memalign_free(ptr);
        }

        static void operator delete[](void* ptr)
        {
            return ::Util::memalign_free(ptr);
        }
    };
}
