#include "commands.hpp"

namespace vkq
{

    CommandPool CommandPool::create(const Device& device, const vk::CommandPoolCreateInfo& createInfo)
    {
        vk::CommandPool commandPool = device.createCommandPool(createInfo);

        return CommandPool{device, commandPool};
    }

    void CommandPool::destory()
    {
        device.destroyCommandPool(commandPool);

        commandPool = nullptr;
        device = {};
    }

    CommandBuffer CommandBuffer::allocate(const CommandPool& commandPool, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next = {})
    {
        vk::CommandBuffer commandBuffer;
        commandPool.allocateCommandBuffers(1, &commandBuffer, level, next);

        return CommandBuffer{commandPool, commandBuffer};
    }

    void CommandBuffer::free()
    {
        commandPool.freeCommandBuffers(commandBuffer);

        commandBuffer = nullptr;
        commandPool = {};
    }

} // namespace vkq
