#pragma once

#include "device.hpp"
#include "queue.hpp"

namespace vkq
{
    class CommandPool
    {
    public:
        CommandPool() = default;
        ~CommandPool() = default;

    public:
        /**
         * @brief Creates a new command pool from a device
         * 
         * @param device Parent device of command pool
         * @param createInfo Info specifying creation of command pool
         * @return Newly created command pool handle
         */
        static CommandPool create(const Device& device, const vk::CommandPoolCreateInfo& createInfo);

        /**
         * @brief Creates a new command pool from a QueueFamily
         * 
         * @param family Implicit parent of command pool
         * @param flags Additional command pool creation flags
         * @param next pNext proxy class allowing vk::CommandPoolCreateInfo to be extended
         * @return Newly created command pool handle
         */
        static CommandPool create(const QueueFamily& family, vk::CommandPoolCreateFlags flags = {}, const VkNextProxy<vk::CommandPoolCreateInfo>& next = {})
        {
            vk::CommandPoolCreateInfo createInfo{};
            createInfo.pNext = next;
            createInfo.flags = flags;
            createInfo.queueFamilyIndex = family.getIndex();

            return create(family.getDevice(), createInfo);
        }

        /**
         * @brief Destroys this command pool
         * 
         */
        void destory();

        //////////////////////////////
        // Core Functions ////////////
        //////////////////////////////

        std::vector<vk::CommandBuffer> allocateCommandBuffers(uint32_t commandBufferCount, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next = {}) const
        {
            vk::CommandBufferAllocateInfo allocInfo{};
            allocInfo.pNext = next;
            allocInfo.commandPool = commandPool;
            allocInfo.level = level;
            allocInfo.commandBufferCount = commandBufferCount;

            return device.allocateCommandBuffers(allocInfo);
        }

        void allocateCommandBuffers(uint32_t commandBufferCount, vk::CommandBuffer* commandBuffers, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next = {}) const
        {
            device.allocateCommandBuffers(commandPool, commandBufferCount, commandBuffers, level, next);
        }

        void freeCommandBuffers(const vk::ArrayProxy<const vk::CommandBuffer>& commandBuffers) const
        {
            device.freeCommandBuffers(commandPool, commandBuffers);
        }

        void reset(vk::CommandPoolResetFlags flags) const
        {
            device.resetCommandPool(commandPool, flags);
        }

        ///////////////////////////////////
        // Version 1.1 ////////////////////
        ///////////////////////////////////

#ifdef VK_VERSION_1_1

        void trim(vk::CommandPoolTrimFlags flags) const
        {
            device.trimCommandPool(commandPool, flags);
        }

#endif

        /////////////////////////////////
        // Extensions ///////////////////
        /////////////////////////////////

#ifdef VK_KHR_MAINTENANCE1_EXTENSION_NAME

        void trimKHR(vk::CommandPoolTrimFlagsKHR flags) const
        {
            device.trimCommandPoolKHR(commandPool, flags);
        }

#endif

        ///////////////////////////////
        // Retrieve handles ///////////
        ///////////////////////////////

        Device getDevice() const
        {
            return device;
        }

        const vk::DispatchLoaderDynamic& getDeviceDispatch() const
        {
            return device.getDeviceDispatch();
        }

        vk::CommandPool vkCommandPool() const
        {
            return commandPool;
        }

        vk::CommandPool vkHandle() const
        {
            return commandPool;
        }

        operator vk::CommandPool() const
        {
            return commandPool;
        }

    private:
        explicit CommandPool(Device device, vk::CommandPool commandPool)
            : device(device), commandPool(commandPool)
        {
        }

        Device device;
        vk::CommandPool commandPool;
    };

    class CommandBuffer
    {
    public:
        CommandBuffer() = default;
        ~CommandBuffer() = default;

    public:
        // /**
        //  * @brief Batch allocates command buffers. Will most likely be faster than calling allocate for each seperate command buffer.
        //  *
        //  * @param commandPool Command Pool to allocate from
        //  * @param commandBufferCount Number of command buffers to allocate
        //  * @param level Command Buffer Level
        //  * @param next pNext proxy class allowing vk::CommandBufferAllocateInfo to be extended
        //  * @return Vector of newly allocated command buffers
        //  */
        // static std::vector<CommandBuffer> allocate(const CommandPool& commandPool, uint32_t commandBufferCount, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next = {});

        // /**
        //  * @brief Batch frees command buffers. Will most likely be faster than calling free for each seperate command buffer.
        //  *
        //  * @param commandPool Command Pool from which all command buffers were originally allocated
        //  * @param commandBuffers
        //  */
        // static void free(const vk::ArrayProxy<CommandBuffer>& commandBuffers);

        /**
         * @brief Allocates a single command buffer from a command pool
         * 
         * @param commandPool Command Pool to allocate from.
         * @param level Command Buffer Level
         * @param next pNext proxy class allowing vk::CommandBufferAllocateInfo to be extended
         * @return Newly allocated command buffer
         */
        static CommandBuffer allocate(const CommandPool& commandPool, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next = {});

        /**
         * @brief Frees this command buffer and its associated memory from its parent command pool
         * 
         */
        void free();

        void begin(const vk::CommandBufferBeginInfo& beginInfo)
        {
            commandBuffer.begin(beginInfo, commandPool.getDeviceDispatch());
        }

        void end()
        {
            commandBuffer.end(commandPool.getDeviceDispatch());
        }

        void reset(vk::CommandBufferResetFlags flags)
        {
            commandBuffer.reset(flags, commandPool.getDeviceDispatch());
        }

        ////////////////////////////////
        // Commands ////////////////////
        ////////////////////////////////

        void beginQuery(vk::QueryPool queryPool, uint32_t query, vk::QueryControlFlags flags) noexcept
        {
            commandBuffer.beginQuery(queryPool, query, flags, commandPool.getDeviceDispatch());
        }

        void beginRenderPass(const vk::RenderPassBeginInfo& renderPassBegin, vk::SubpassContents contents) noexcept
        {
            commandBuffer.beginRenderPass(renderPassBegin, contents, commandPool.getDeviceDispatch());
        }

        void bindDescriptorSets(vk::PipelineBindPoint pipelineBindPoint, vk::PipelineLayout layout, uint32_t firstSet, const vk::ArrayProxy<const vk::DescriptorSet>& descriptorSets, const vk::ArrayProxy<const uint32_t>& dynamicOffsets) noexcept
        {
            commandBuffer.bindDescriptorSets(pipelineBindPoint, layout, firstSet, descriptorSets, dynamicOffsets, commandPool.getDeviceDispatch());
        }

        void bindIndexBuffer(vk::Buffer buffer, vk::DeviceSize offset, vk::IndexType indexType) noexcept
        {
            commandBuffer.bindIndexBuffer(buffer, offset, indexType, commandPool.getDeviceDispatch());
        }

        void bindPipeline(vk::PipelineBindPoint pipelineBindPoint, vk::Pipeline pipeline) noexcept
        {
            commandBuffer.bindPipeline(pipelineBindPoint, pipeline, commandPool.getDeviceDispatch());
        }

        void bindVertexBuffers(uint32_t firstBinding, vk::ArrayProxy<const vk::Buffer> const& buffers, vk::ArrayProxy<const vk::DeviceSize> const& offsets) noexcept
        {
            commandBuffer.bindVertexBuffers(firstBinding, buffers, offsets, commandPool.getDeviceDispatch());
        }

        void blitImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::ImageBlit> const& regions, vk::Filter filter) noexcept
        {
            commandBuffer.blitImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions, filter, commandPool.getDeviceDispatch());
        }

        void clearAttachments(vk::ArrayProxy<const vk::ClearAttachment> const& attachments, vk::ArrayProxy<const vk::ClearRect> const& rects) noexcept
        {
            commandBuffer.clearAttachments(attachments, rects, commandPool.getDeviceDispatch());
        }

        void clearColorImage(vk::Image image, vk::ImageLayout imageLayout, const vk::ClearColorValue& color, vk::ArrayProxy<const vk::ImageSubresourceRange> const& ranges) noexcept
        {
            commandBuffer.clearColorImage(image, imageLayout, color, ranges, commandPool.getDeviceDispatch());
        }

        void clearDepthStencilImage(vk::Image image, vk::ImageLayout imageLayout, const vk::ClearDepthStencilValue& depthStencil, vk::ArrayProxy<const vk::ImageSubresourceRange> const& ranges) noexcept
        {
            commandBuffer.clearDepthStencilImage(image, imageLayout, depthStencil, ranges, commandPool.getDeviceDispatch());
        }

        void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::ArrayProxy<const vk::BufferCopy> const& regions) noexcept
        {
            commandBuffer.copyBuffer(srcBuffer, dstBuffer, regions, commandPool.getDeviceDispatch());
        }

        void copyBufferToImage(vk::Buffer srcBuffer, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::BufferImageCopy> const& regions) noexcept
        {
            commandBuffer.copyBufferToImage(srcBuffer, dstImage, dstImageLayout, regions, commandPool.getDeviceDispatch());
        }

        void copyImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::ImageCopy> const& regions) noexcept
        {
            commandBuffer.copyImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions, commandPool.getDeviceDispatch());
        }

        void copyImageToBuffer(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Buffer dstBuffer, vk::ArrayProxy<const vk::BufferImageCopy> const& regions) noexcept
        {
            commandBuffer.copyImageToBuffer(srcImage, srcImageLayout, dstBuffer, regions, commandPool.getDeviceDispatch());
        }

        void copyQueryPoolResults(vk::QueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize stride, vk::QueryResultFlags flags) noexcept
        {
            commandBuffer.copyQueryPoolResults(queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags, commandPool.getDeviceDispatch());
        }

        void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) noexcept
        {
            commandBuffer.dispatch(groupCountX, groupCountY, groupCountZ, commandPool.getDeviceDispatch());
        }

        void dispatchIndirect(vk::Buffer buffer, vk::DeviceSize offset) noexcept
        {
            commandBuffer.dispatchIndirect(buffer, offset, commandPool.getDeviceDispatch());
        }

        void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) noexcept
        {
            commandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance, commandPool.getDeviceDispatch());
        }

        void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) noexcept
        {
            commandBuffer.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, commandPool.getDeviceDispatch());
        }

        void drawIndexedIndirect(vk::Buffer buffer, vk::DeviceSize offset, uint32_t drawCount, uint32_t stride) noexcept
        {
            commandBuffer.drawIndexedIndirect(buffer, offset, drawCount, stride, commandPool.getDeviceDispatch());
        }

        void drawIndirect(vk::Buffer buffer, vk::DeviceSize offset, uint32_t drawCount, uint32_t stride) noexcept
        {
            commandBuffer.drawIndirect(buffer, offset, drawCount, stride, commandPool.getDeviceDispatch());
        }

        void endQuery(vk::QueryPool queryPool, uint32_t query) noexcept
        {
            commandBuffer.endQuery(queryPool, query, commandPool.getDeviceDispatch());
        }

        void endRenderPass() noexcept
        {
            commandBuffer.endRenderPass(commandPool.getDeviceDispatch());
        }

        void executeCommands(vk::ArrayProxy<const vk::CommandBuffer> const& commandBuffers) noexcept
        {
            commandBuffer.executeCommands(commandBuffers, commandPool.getDeviceDispatch());
        }

        void fillBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize size, uint32_t data) noexcept
        {
            commandBuffer.fillBuffer(dstBuffer, dstOffset, size, data, commandPool.getDeviceDispatch());
        }

        void nextSubpass(vk::SubpassContents contents) noexcept
        {
            commandBuffer.nextSubpass(contents, commandPool.getDeviceDispatch());
        }

        void pipelineBarrier(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::DependencyFlags dependencyFlags, vk::ArrayProxy<const vk::MemoryBarrier> const& memoryBarriers, vk::ArrayProxy<const vk::BufferMemoryBarrier> const& bufferMemoryBarriers, vk::ArrayProxy<const vk::ImageMemoryBarrier> const& imageMemoryBarriers) noexcept
        {
            commandBuffer.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags, memoryBarriers, bufferMemoryBarriers, imageMemoryBarriers, commandPool.getDeviceDispatch());
        }

        void pushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) noexcept
        {
            commandBuffer.pushConstants(layout, stageFlags, offset, size, pValues, commandPool.getDeviceDispatch());
        }

        void resetEvent(vk::Event event, vk::PipelineStageFlags stageMask) noexcept
        {
            commandBuffer.resetEvent(event, stageMask, commandPool.getDeviceDispatch());
        }

        void resetQueryPool(vk::QueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) noexcept
        {
            commandBuffer.resetQueryPool(queryPool, firstQuery, queryCount, commandPool.getDeviceDispatch());
        }

        void resolveImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::ImageResolve> const& regions) noexcept
        {
            commandBuffer.resolveImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions, commandPool.getDeviceDispatch());
        }

        void setBlendConstants(const float blendConstants[4]) noexcept
        {
            commandBuffer.setBlendConstants(blendConstants, commandPool.getDeviceDispatch());
        }

        void setDepthBias(float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) noexcept
        {
            commandBuffer.setDepthBias(depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor, commandPool.getDeviceDispatch());
        }

        void setDepthBounds(float minDepthBounds, float maxDepthBounds) noexcept
        {
            commandBuffer.setDepthBounds(minDepthBounds, maxDepthBounds, commandPool.getDeviceDispatch());
        }

        void setEvent(vk::Event event, vk::PipelineStageFlags stageMask) noexcept
        {
            commandBuffer.setEvent(event, stageMask, commandPool.getDeviceDispatch());
        }

        void setLineWidth(float lineWidth) noexcept
        {
            commandBuffer.setLineWidth(lineWidth, commandPool.getDeviceDispatch());
        }

        void setScissor(uint32_t firstScissor, vk::ArrayProxy<const vk::Rect2D> const& scissors) noexcept
        {
            commandBuffer.setScissor(firstScissor, scissors, commandPool.getDeviceDispatch());
        }

        void setStencilCompareMask(vk::StencilFaceFlags faceMask, uint32_t compareMask) noexcept
        {
            commandBuffer.setStencilCompareMask(faceMask, compareMask, commandPool.getDeviceDispatch());
        }

        void setViewport(uint32_t firstViewport, vk::ArrayProxy<const vk::Viewport> const& viewports) noexcept
        {
            commandBuffer.setViewport(firstViewport, viewports, commandPool.getDeviceDispatch());
        }

        void updateBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize dataSize, const void* pData) noexcept
        {
            commandBuffer.updateBuffer(dstBuffer, dstOffset, dataSize, pData, commandPool.getDeviceDispatch());
        }

        void waitEvents(vk::ArrayProxy<const vk::Event> const& events, vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::ArrayProxy<const vk::MemoryBarrier> const& memoryBarriers, vk::ArrayProxy<const vk::BufferMemoryBarrier> const& bufferMemoryBarriers, vk::ArrayProxy<const vk::ImageMemoryBarrier> const& imageMemoryBarriers) noexcept
        {
            commandBuffer.waitEvents(events, srcStageMask, dstStageMask, memoryBarriers, bufferMemoryBarriers, imageMemoryBarriers, commandPool.getDeviceDispatch());
        }

        void writeTimestamp(vk::PipelineStageFlagBits pipelineStage, vk::QueryPool queryPool, uint32_t query) noexcept
        {
            commandBuffer.writeTimestamp(pipelineStage, queryPool, query, commandPool.getDeviceDispatch());
        }

        ////////////////////////////////
        // Helper alias functions //////
        ////////////////////////////////

        template <typename T>
        void pushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stageFlags, uint32_t offset, vk::ArrayProxy<const T> const& values) noexcept
        {
            pushConstants(layouts, stageFlags, offset, values.size() * sizeof(T), reinterpret_cast<const void*>(values.data()));
        }

        template <typename T>
        void updateBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::ArrayProxy<const T> const& data) noexcept
        {
            updateBuffer(dstBuffer, dstOffset, data.size() * sizeof(T), reinterpret_cast<const void*>(data.data()));
        }

        void begin(vk::CommandBufferUsageFlags flags, vk::Optional<const vk::CommandBufferInheritanceInfo> inheritanceInfo = nullptr, const VkNextProxy<vk::CommandBufferBeginInfo>& next = {})
        {
            vk::CommandBufferBeginInfo beginInfo{};
            beginInfo.pNext = next;
            beginInfo.pInheritanceInfo = inheritanceInfo;
            beginInfo.flags = flags;

            begin(beginInfo);
        }

        ////////////////////////////////
        // Native vk handles ///////////
        ////////////////////////////////

        const vk::DispatchLoaderDynamic& getDeviceDispatch() const
        {
            return commandPool.getDeviceDispatch();
        }

        vk::CommandBuffer vkCommandBuffer() const
        {
            return commandBuffer;
        }

        vk::CommandBuffer vkHandle() const
        {
            return commandBuffer;
        }

        operator vk::CommandBuffer() const
        {
            return commandBuffer;
        }

    private:
        explicit CommandBuffer(const CommandPool& commandPool, vk::CommandBuffer commandBuffer)
            : commandPool(commandPool), commandBuffer(commandBuffer)
        {
        }

        CommandPool commandPool;
        vk::CommandBuffer commandBuffer;
    };
} // namespace vkq
