#pragma once

#define VK_NO_PROTOTYPES

#include <vulkan/vulkan.hpp>

namespace vkq
{
    /**
    * @brief Abstracts extending Vulkan structures
    */
    template <typename BaseConfigType>
    class VkNextProxy
    {
    public:
        VkNextProxy() : next_(nullptr) {}

        template <typename NextConfigType>
        VkNextProxy(const NextConfigType& next) : next_(&next)
        {
            static_assert(vk::isStructureChainValid<BaseConfigType, NextConfigType>::value,
                          "NextConfigType must extend BaseConfigType");
        }

        operator const void*() const
        {
            return next_;
        }

    private:
        const void* next_;
    };
}