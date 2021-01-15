#pragma once

#include <cstdlib>
#include <vector>

namespace vkq
{

    template <typename T>
    class ObjectPool
    {
        /**
         * @brief When allocated this has enough space to hold one object of type T.
         * Otherwise stores a value pointing to the next free item in the item block.
         * 
         */
        union Item
        {
            uint32_t nextFreeIndex;
            alignas(T) char value[sizeof(T)];
        };

        /**
         * @brief An block of allocated items.
         * Holds free items as a singlely linked list.
         */
        struct ItemBlock
        {
            uint32_t capacity;
            Item* pItems;
            uint32_t firstFreeIndex;
        };

    public:
        ObjectPool(uint32_t initialCapacity)
            : m_InitialCapacity(initialCapacity)
        {
        }

        ObjectPool()
            : m_InitialCapacity(32)
        {
        }

        ~ObjectPool()
        {
            for (size_t i = m_ItemBlocks.size(); i--;)
                std::free(m_ItemBlocks[i].pItems, m_ItemBlocks[i].capacity);
            m_ItemBlocks.clear();
        }

        template <typename... Types>
        T* alloc(Types... args)
        {
            for (size_t i = m_ItemBlocks.size(); i--;)
            {
                ItemBlock& block = m_ItemBlocks[i];
                // This block has some free items: Use first one.
                if (block.firstFreeIndex != UINT32_MAX)
                {
                    Item* const pItem = &block.pItems[block.firstFreeIndex];
                    block.firstFreeIndex = pItem->nextFreeIndex;
                    T* result = (T*)&pItem->value;
                    new (result) T(std::forward<Types>(args)...); // Explicit constructor call.
                    return result;
                }
            }

            // No block has free item: Create new one and use it.
            ItemBlock& newBlock = createNewBlock();
            Item* const pItem = &newBlock.pItems[0];
            newBlock.firstFreeIndex = pItem->nextFreeIndex;
            T* result = (T*)&pItem->value;
            new (result) T(std::forward<Types>(args)...); // Explicit constructor call.
            return result;
        }

        void free(T* ptr)
        {
            // Search all memory blocks to find ptr.
            for (size_t i = m_ItemBlocks.size(); i--;)
            {
                ItemBlock& block = m_ItemBlocks[i];

                // Casting to union.
                Item* pItemPtr;
                memcpy(&pItemPtr, &ptr, sizeof(pItemPtr));

                // Check if pItemPtr is in address range of this block.
                if ((block.pItems <= pItemPtr) && (pItemPtr < block.pItems + block.capacity))
                {
                    ptr->~T(); // Explicit destructor call.
                    const uint32_t index = static_cast<uint32_t>(pItemPtr - block.pItems);
                    pItemPtr->nextFreeIndex = block.firstFreeIndex;
                    block.firstFreeIndex = index;
                    return;
                }
            }
        }

    private:
        ItemBlock& createNewBlock()
        {
            const uint32_t newBlockCapacity = m_ItemBlocks.empty() ? m_InitialCapacity : uint32_t(m_ItemBlocks.back().capacity * 2);

            ItemBlock newBlock;
            newBlock.capacity = newBlockCapacity;
            newBlock.pItems = std::malloc(sizeof(Item) * newBlockCapacity);
            newBlock.firstFreeIndex = 0;

            m_ItemBlocks.push_back(newBlock);

            // Setup singly-linked list of all free items in this block.
            for (uint32_t i = 0; i < newBlockCapacity - 1; ++i)
                newBlock.pItems[i].nextFreeIndex = i + 1;
            newBlock.pItems[newBlockCapacity - 1].nextFreeIndex = UINT32_MAX;
            return m_ItemBlocks.back();
        }

        uint32_t m_InitialCapacity;
        std::vector<ItemBlock> m_ItemBlocks;
    };

} // namespace vkq
