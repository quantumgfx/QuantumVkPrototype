#include "commands.hpp"

namespace vkq
{

    void CommandBuffer::allocate(const CommandPool& commandPool, uint32_t commandBufferCount, CommandBuffer* commandBuffers, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next)
    {
        commandPool.impl->tempCommandStorage.resize(commandBufferCount);
        commandPool.allocateCommandBuffers(commandBufferCount, commandPool.impl->tempCommandStorage.data(), level, next);

        for (uint32_t i = 0; i < commandBufferCount; i++)
        {
            commandBuffers[i] = CommandBuffer{commandPool.getDevice(), commandPool.impl->tempCommandStorage[i]};
        }
    }

    void CommandBuffer::free(const CommandPool& commandPool, const vk::ArrayProxy<CommandBuffer>& commandBuffers)
    {
        commandPool.impl->tempCommandStorage.resize(commandBuffers.size());

        for (uint32_t i = 0; i < commandBuffers.size(); i++)
        {
            commandPool.impl->tempCommandStorage[i] = commandBuffers.data()[i];
        }

        commandPool.freeCommandBuffers({commandBuffers.size(), commandPool.impl->tempCommandStorage.data()});
    }

    CommandPool CommandPool::create(const Device& device, const vk::CommandPoolCreateInfo& createInfo)
    {
        CommandPool::Impl* impl = new CommandPool::Impl();
        impl->device = device;
        impl->commandPool = device.createCommandPool(createInfo);
        impl->tempCommandStorage.resize(1);

        return CommandPool(impl);
    }

    void CommandPool::destory()
    {
        impl->device.destroyCommandPool(impl->commandPool);

        delete impl;
        impl = nullptr;
    }

    Device CommandPool::getDevice() const
    {
        return impl->device;
    }

    const vk::DispatchLoaderDynamic& CommandPool::getDeviceDispatch() const
    {
        return impl->device.getDeviceDispatch();
    }

    vk::CommandPool CommandPool::vkCommandPool() const
    {
        return impl->commandPool;
    }

    vk::CommandPool CommandPool::vkHandle() const
    {
        return impl->commandPool;
    }

    CommandPool::operator vk::CommandPool() const
    {
        return impl->commandPool;
    }

} // namespace vkq
