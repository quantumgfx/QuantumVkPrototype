#include "commands.hpp"

namespace vkq
{

    void CommandBuffer::allocate(const CommandPool& commandPool, uint32_t commandBufferCount, CommandBuffer* commandBuffers, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next)
    {
        commandPool.impl_->tempCommandStorage.resize(commandBufferCount);
        commandPool.allocateCommandBuffers(commandBufferCount, commandPool.impl_->tempCommandStorage.data(), level, next);

        for (uint32_t i = 0; i < commandBufferCount; i++)
        {
            commandBuffers[i] = CommandBuffer{commandPool.device(), commandPool.impl_->tempCommandStorage[i]};
        }
    }

    void CommandBuffer::free(const CommandPool& commandPool, const vk::ArrayProxy<CommandBuffer>& commandBuffers)
    {
        commandPool.impl_->tempCommandStorage.resize(commandBuffers.size());

        for (uint32_t i = 0; i < commandBuffers.size(); i++)
        {
            commandPool.impl_->tempCommandStorage[i] = commandBuffers.data()[i];
        }

        commandPool.freeCommandBuffers({commandBuffers.size(), commandPool.impl_->tempCommandStorage.data()});
    }

    explicit CommandPool::CommandPool(CommandPool::Impl* impl_)
        : impl_(impl_)
    {
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
        impl_->device.destroyCommandPool(impl_->commandPool);

        delete impl_;
        impl_ = nullptr;
    }

    Device CommandPool::device() const
    {
        return impl_->device;
    }

    const vk::DispatchLoaderDynamic& CommandPool::dispatch() const
    {
        return impl_->device.dispatch();
    }

    vk::CommandPool CommandPool::vkCommandPool() const
    {
        return impl_->commandPool;
    }

    vk::CommandPool CommandPool::vkHandle() const
    {
        return impl_->commandPool;
    }

    CommandPool::operator vk::CommandPool() const
    {
        return impl_->commandPool;
    }

} // namespace vkq
