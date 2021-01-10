#pragma once

#include "device.hpp"
#include "queue.hpp"

namespace vkq
{

    class CommandPool;

    class CommandBuffer
    {
    public:
        CommandBuffer() = default;
        ~CommandBuffer() = default;

    public:
        /**
         * @brief Batch allocates command buffers. Will most likely be faster than calling allocate for each seperate command buffer.
         *
         * @param commandPool Command Pool to allocate from
         * @param commandBufferCount Number of command buffers to allocate
         * @param level Command Buffer Level
         * @param next pNext proxy class allowing vk::CommandBufferAllocateInfo to be extended
         * @return Vector of newly allocated command buffers
         */
        static void allocate(const CommandPool& commandPool, uint32_t commandBufferCount, CommandBuffer* commandBuffers, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next = {});

        /**
         * @brief Batch frees command buffers. Will most likely be faster than calling free for each seperate command buffer.
         *
         * @param commandPool Command Pool from which all command buffers were originally allocated
         * @param commandBuffers
         */
        static void free(const CommandPool& commandPool, const vk::ArrayProxy<CommandBuffer>& commandBuffers);

        //static CommandBuffer allocate(const CommandPool& commandPool, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next = {});

        //static void free(const CommandPool& commandPool, CommandBuffer commandBuffer);

        void begin(const vk::CommandBufferBeginInfo& beginInfo)
        {
            commandBuffer.begin(beginInfo, device.getDeviceDispatch());
        }

        void end()
        {
            commandBuffer.end(device.getDeviceDispatch());
        }

        void reset(vk::CommandBufferResetFlags flags)
        {
            commandBuffer.reset(flags, device.getDeviceDispatch());
        }

        ////////////////////////////////
        // Commands ////////////////////
        ////////////////////////////////

        void beginQuery(vk::QueryPool queryPool, uint32_t query, vk::QueryControlFlags flags) noexcept
        {
            commandBuffer.beginQuery(queryPool, query, flags, device.getDeviceDispatch());
        }

        void beginRenderPass(const vk::RenderPassBeginInfo& renderPassBegin, vk::SubpassContents contents) noexcept
        {
            commandBuffer.beginRenderPass(renderPassBegin, contents, device.getDeviceDispatch());
        }

        void bindDescriptorSets(vk::PipelineBindPoint pipelineBindPoint, vk::PipelineLayout layout, uint32_t firstSet, const vk::ArrayProxy<const vk::DescriptorSet>& descriptorSets, const vk::ArrayProxy<const uint32_t>& dynamicOffsets) noexcept
        {
            commandBuffer.bindDescriptorSets(pipelineBindPoint, layout, firstSet, descriptorSets, dynamicOffsets, device.getDeviceDispatch());
        }

        void bindIndexBuffer(vk::Buffer buffer, vk::DeviceSize offset, vk::IndexType indexType) noexcept
        {
            commandBuffer.bindIndexBuffer(buffer, offset, indexType, device.getDeviceDispatch());
        }

        void bindPipeline(vk::PipelineBindPoint pipelineBindPoint, vk::Pipeline pipeline) noexcept
        {
            commandBuffer.bindPipeline(pipelineBindPoint, pipeline, device.getDeviceDispatch());
        }

        void bindVertexBuffers(uint32_t firstBinding, vk::ArrayProxy<const vk::Buffer> const& buffers, vk::ArrayProxy<const vk::DeviceSize> const& offsets) noexcept
        {
            commandBuffer.bindVertexBuffers(firstBinding, buffers, offsets, device.getDeviceDispatch());
        }

        void blitImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::ImageBlit> const& regions, vk::Filter filter) noexcept
        {
            commandBuffer.blitImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions, filter, device.getDeviceDispatch());
        }

        void clearAttachments(vk::ArrayProxy<const vk::ClearAttachment> const& attachments, vk::ArrayProxy<const vk::ClearRect> const& rects) noexcept
        {
            commandBuffer.clearAttachments(attachments, rects, device.getDeviceDispatch());
        }

        void clearColorImage(vk::Image image, vk::ImageLayout imageLayout, const vk::ClearColorValue& color, vk::ArrayProxy<const vk::ImageSubresourceRange> const& ranges) noexcept
        {
            commandBuffer.clearColorImage(image, imageLayout, color, ranges, device.getDeviceDispatch());
        }

        void clearDepthStencilImage(vk::Image image, vk::ImageLayout imageLayout, const vk::ClearDepthStencilValue& depthStencil, vk::ArrayProxy<const vk::ImageSubresourceRange> const& ranges) noexcept
        {
            commandBuffer.clearDepthStencilImage(image, imageLayout, depthStencil, ranges, device.getDeviceDispatch());
        }

        void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::ArrayProxy<const vk::BufferCopy> const& regions) noexcept
        {
            commandBuffer.copyBuffer(srcBuffer, dstBuffer, regions, device.getDeviceDispatch());
        }

        void copyBufferToImage(vk::Buffer srcBuffer, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::BufferImageCopy> const& regions) noexcept
        {
            commandBuffer.copyBufferToImage(srcBuffer, dstImage, dstImageLayout, regions, device.getDeviceDispatch());
        }

        void copyImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::ImageCopy> const& regions) noexcept
        {
            commandBuffer.copyImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions, device.getDeviceDispatch());
        }

        void copyImageToBuffer(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Buffer dstBuffer, vk::ArrayProxy<const vk::BufferImageCopy> const& regions) noexcept
        {
            commandBuffer.copyImageToBuffer(srcImage, srcImageLayout, dstBuffer, regions, device.getDeviceDispatch());
        }

        void copyQueryPoolResults(vk::QueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize stride, vk::QueryResultFlags flags) noexcept
        {
            commandBuffer.copyQueryPoolResults(queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags, device.getDeviceDispatch());
        }

        void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) noexcept
        {
            commandBuffer.dispatch(groupCountX, groupCountY, groupCountZ, device.getDeviceDispatch());
        }

        void dispatchIndirect(vk::Buffer buffer, vk::DeviceSize offset) noexcept
        {
            commandBuffer.dispatchIndirect(buffer, offset, device.getDeviceDispatch());
        }

        void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) noexcept
        {
            commandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance, device.getDeviceDispatch());
        }

        void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) noexcept
        {
            commandBuffer.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, device.getDeviceDispatch());
        }

        void drawIndexedIndirect(vk::Buffer buffer, vk::DeviceSize offset, uint32_t drawCount, uint32_t stride) noexcept
        {
            commandBuffer.drawIndexedIndirect(buffer, offset, drawCount, stride, device.getDeviceDispatch());
        }

        void drawIndirect(vk::Buffer buffer, vk::DeviceSize offset, uint32_t drawCount, uint32_t stride) noexcept
        {
            commandBuffer.drawIndirect(buffer, offset, drawCount, stride, device.getDeviceDispatch());
        }

        void endQuery(vk::QueryPool queryPool, uint32_t query) noexcept
        {
            commandBuffer.endQuery(queryPool, query, device.getDeviceDispatch());
        }

        void endRenderPass() noexcept
        {
            commandBuffer.endRenderPass(device.getDeviceDispatch());
        }

        void executeCommands(vk::ArrayProxy<const vk::CommandBuffer> const& commandBuffers) noexcept
        {
            commandBuffer.executeCommands(commandBuffers, device.getDeviceDispatch());
        }

        void fillBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize size, uint32_t data) noexcept
        {
            commandBuffer.fillBuffer(dstBuffer, dstOffset, size, data, device.getDeviceDispatch());
        }

        void nextSubpass(vk::SubpassContents contents) noexcept
        {
            commandBuffer.nextSubpass(contents, device.getDeviceDispatch());
        }

        void pipelineBarrier(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::DependencyFlags dependencyFlags, vk::ArrayProxy<const vk::MemoryBarrier> const& memoryBarriers, vk::ArrayProxy<const vk::BufferMemoryBarrier> const& bufferMemoryBarriers, vk::ArrayProxy<const vk::ImageMemoryBarrier> const& imageMemoryBarriers) noexcept
        {
            commandBuffer.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags, memoryBarriers, bufferMemoryBarriers, imageMemoryBarriers, device.getDeviceDispatch());
        }

        void pushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) noexcept
        {
            commandBuffer.pushConstants(layout, stageFlags, offset, size, pValues, device.getDeviceDispatch());
        }

        void resetEvent(vk::Event event, vk::PipelineStageFlags stageMask) noexcept
        {
            commandBuffer.resetEvent(event, stageMask, device.getDeviceDispatch());
        }

        void resetQueryPool(vk::QueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) noexcept
        {
            commandBuffer.resetQueryPool(queryPool, firstQuery, queryCount, device.getDeviceDispatch());
        }

        void resolveImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::ImageResolve> const& regions) noexcept
        {
            commandBuffer.resolveImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions, device.getDeviceDispatch());
        }

        void setBlendConstants(const float blendConstants[4]) noexcept
        {
            commandBuffer.setBlendConstants(blendConstants, device.getDeviceDispatch());
        }

        void setDepthBias(float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) noexcept
        {
            commandBuffer.setDepthBias(depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor, device.getDeviceDispatch());
        }

        void setDepthBounds(float minDepthBounds, float maxDepthBounds) noexcept
        {
            commandBuffer.setDepthBounds(minDepthBounds, maxDepthBounds, device.getDeviceDispatch());
        }

        void setEvent(vk::Event event, vk::PipelineStageFlags stageMask) noexcept
        {
            commandBuffer.setEvent(event, stageMask, device.getDeviceDispatch());
        }

        void setLineWidth(float lineWidth) noexcept
        {
            commandBuffer.setLineWidth(lineWidth, device.getDeviceDispatch());
        }

        void setScissor(uint32_t firstScissor, vk::ArrayProxy<const vk::Rect2D> const& scissors) noexcept
        {
            commandBuffer.setScissor(firstScissor, scissors, device.getDeviceDispatch());
        }

        void setStencilCompareMask(vk::StencilFaceFlags faceMask, uint32_t compareMask) noexcept
        {
            commandBuffer.setStencilCompareMask(faceMask, compareMask, device.getDeviceDispatch());
        }

        void setViewport(uint32_t firstViewport, vk::ArrayProxy<const vk::Viewport> const& viewports) noexcept
        {
            commandBuffer.setViewport(firstViewport, viewports, device.getDeviceDispatch());
        }

        void updateBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize dataSize, const void* pData) noexcept
        {
            commandBuffer.updateBuffer(dstBuffer, dstOffset, dataSize, pData, device.getDeviceDispatch());
        }

        void waitEvents(vk::ArrayProxy<const vk::Event> const& events, vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::ArrayProxy<const vk::MemoryBarrier> const& memoryBarriers, vk::ArrayProxy<const vk::BufferMemoryBarrier> const& bufferMemoryBarriers, vk::ArrayProxy<const vk::ImageMemoryBarrier> const& imageMemoryBarriers) noexcept
        {
            commandBuffer.waitEvents(events, srcStageMask, dstStageMask, memoryBarriers, bufferMemoryBarriers, imageMemoryBarriers, device.getDeviceDispatch());
        }

        void writeTimestamp(vk::PipelineStageFlagBits pipelineStage, vk::QueryPool queryPool, uint32_t query) noexcept
        {
            commandBuffer.writeTimestamp(pipelineStage, queryPool, query, device.getDeviceDispatch());
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
            return device.getDeviceDispatch();
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
        explicit CommandBuffer(Device device, vk::CommandBuffer commandBuffer)
            : device(device), commandBuffer(commandBuffer)
        {
        }

        Device device;
        vk::CommandBuffer commandBuffer;
    };

    /**
     * @brief Handle representing vk::CommandPool. This must be syncronized externally, and it is recommended to have at least one command pool 
     * for each thread recording commands.
     */
    class CommandPool
    {
        struct Impl
        {
            Device device;
            vk::CommandPool commandPool;
            std::vector<vk::CommandBuffer> tempCommandStorage;
        };

        friend class CommandBuffer;

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

        void allocateCommandBuffers(uint32_t commandBufferCount, vk::CommandBuffer* commandBuffers, vk::CommandBufferLevel level, const VkNextProxy<vk::CommandBufferAllocateInfo>& next = {}) const
        {
            getDevice().allocateCommandBuffers(vkCommandPool(), commandBufferCount, commandBuffers, level, next);
        }

        void freeCommandBuffers(const vk::ArrayProxy<const vk::CommandBuffer>& commandBuffers) const
        {
            getDevice().freeCommandBuffers(vkCommandPool(), commandBuffers);
        }

        void reset(vk::CommandPoolResetFlags flags) const
        {
            getDevice().resetCommandPool(vkCommandPool(), flags);
        }

        ///////////////////////////////////
        // Version 1.1 ////////////////////
        ///////////////////////////////////

#ifdef VK_VERSION_1_1

        void trim(vk::CommandPoolTrimFlags flags) const
        {
            getDevice().trimCommandPool(vkCommandPool(), flags);
        }

#endif

        /////////////////////////////////
        // Extensions ///////////////////
        /////////////////////////////////

#ifdef VK_KHR_MAINTENANCE1_EXTENSION_NAME

        void trimKHR(vk::CommandPoolTrimFlagsKHR flags) const
        {
            getDevice().trimCommandPoolKHR(vkCommandPool(), flags);
        }

#endif

        ///////////////////////////////
        // Retrieve handles ///////////
        ///////////////////////////////

        Device getDevice() const;
        const vk::DispatchLoaderDynamic& getDeviceDispatch() const;

        vk::CommandPool vkCommandPool() const;
        vk::CommandPool vkHandle() const;
        operator vk::CommandPool() const;

    private:
        explicit CommandPool(Impl* impl)
            : impl(impl)
        {
        }

        Impl* impl = nullptr;
    };

} // namespace vkq
