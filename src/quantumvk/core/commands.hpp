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
            commandBuffer_.begin(beginInfo, device_.dispatch());
        }

        void end()
        {
            commandBuffer_.end(device_.dispatch());
        }

        void reset(vk::CommandBufferResetFlags flags)
        {
            commandBuffer_.reset(flags, device_.dispatch());
        }

        ////////////////////////////////
        // Commands ////////////////////
        ////////////////////////////////

        void beginQuery(vk::QueryPool queryPool, uint32_t query, vk::QueryControlFlags flags) noexcept
        {
            commandBuffer_.beginQuery(queryPool, query, flags, device_.dispatch());
        }

        void beginRenderPass(const vk::RenderPassBeginInfo& renderPassBegin, vk::SubpassContents contents) noexcept
        {
            commandBuffer_.beginRenderPass(renderPassBegin, contents, device_.dispatch());
        }

        void bindDescriptorSets(vk::PipelineBindPoint pipelineBindPoint, vk::PipelineLayout layout, uint32_t firstSet, const vk::ArrayProxy<const vk::DescriptorSet>& descriptorSets, const vk::ArrayProxy<const uint32_t>& dynamicOffsets) noexcept
        {
            commandBuffer_.bindDescriptorSets(pipelineBindPoint, layout, firstSet, descriptorSets, dynamicOffsets, device_.dispatch());
        }

        void bindIndexBuffer(vk::Buffer buffer, vk::DeviceSize offset, vk::IndexType indexType) noexcept
        {
            commandBuffer_.bindIndexBuffer(buffer, offset, indexType, device_.dispatch());
        }

        void bindPipeline(vk::PipelineBindPoint pipelineBindPoint, vk::Pipeline pipeline) noexcept
        {
            commandBuffer_.bindPipeline(pipelineBindPoint, pipeline, device_.dispatch());
        }

        void bindVertexBuffers(uint32_t firstBinding, vk::ArrayProxy<const vk::Buffer> const& buffers, vk::ArrayProxy<const vk::DeviceSize> const& offsets) noexcept
        {
            commandBuffer_.bindVertexBuffers(firstBinding, buffers, offsets, device_.dispatch());
        }

        void blitImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::ImageBlit> const& regions, vk::Filter filter) noexcept
        {
            commandBuffer_.blitImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions, filter, device_.dispatch());
        }

        void clearAttachments(vk::ArrayProxy<const vk::ClearAttachment> const& attachments, vk::ArrayProxy<const vk::ClearRect> const& rects) noexcept
        {
            commandBuffer_.clearAttachments(attachments, rects, device_.dispatch());
        }

        void clearColorImage(vk::Image image, vk::ImageLayout imageLayout, const vk::ClearColorValue& color, vk::ArrayProxy<const vk::ImageSubresourceRange> const& ranges) noexcept
        {
            commandBuffer_.clearColorImage(image, imageLayout, color, ranges, device_.dispatch());
        }

        void clearDepthStencilImage(vk::Image image, vk::ImageLayout imageLayout, const vk::ClearDepthStencilValue& depthStencil, vk::ArrayProxy<const vk::ImageSubresourceRange> const& ranges) noexcept
        {
            commandBuffer_.clearDepthStencilImage(image, imageLayout, depthStencil, ranges, device_.dispatch());
        }

        void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::ArrayProxy<const vk::BufferCopy> const& regions) noexcept
        {
            commandBuffer_.copyBuffer(srcBuffer, dstBuffer, regions, device_.dispatch());
        }

        void copyBufferToImage(vk::Buffer srcBuffer, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::BufferImageCopy> const& regions) noexcept
        {
            commandBuffer_.copyBufferToImage(srcBuffer, dstImage, dstImageLayout, regions, device_.dispatch());
        }

        void copyImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::ImageCopy> const& regions) noexcept
        {
            commandBuffer_.copyImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions, device_.dispatch());
        }

        void copyImageToBuffer(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Buffer dstBuffer, vk::ArrayProxy<const vk::BufferImageCopy> const& regions) noexcept
        {
            commandBuffer_.copyImageToBuffer(srcImage, srcImageLayout, dstBuffer, regions, device_.dispatch());
        }

        void copyQueryPoolResults(vk::QueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize stride, vk::QueryResultFlags flags) noexcept
        {
            commandBuffer_.copyQueryPoolResults(queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags, device_.dispatch());
        }

        void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) noexcept
        {
            commandBuffer_.dispatch(groupCountX, groupCountY, groupCountZ, device_.dispatch());
        }

        void dispatchIndirect(vk::Buffer buffer, vk::DeviceSize offset) noexcept
        {
            commandBuffer_.dispatchIndirect(buffer, offset, device_.dispatch());
        }

        void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) noexcept
        {
            commandBuffer_.draw(vertexCount, instanceCount, firstVertex, firstInstance, device_.dispatch());
        }

        void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) noexcept
        {
            commandBuffer_.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, device_.dispatch());
        }

        void drawIndexedIndirect(vk::Buffer buffer, vk::DeviceSize offset, uint32_t drawCount, uint32_t stride) noexcept
        {
            commandBuffer_.drawIndexedIndirect(buffer, offset, drawCount, stride, device_.dispatch());
        }

        void drawIndirect(vk::Buffer buffer, vk::DeviceSize offset, uint32_t drawCount, uint32_t stride) noexcept
        {
            commandBuffer_.drawIndirect(buffer, offset, drawCount, stride, device_.dispatch());
        }

        void endQuery(vk::QueryPool queryPool, uint32_t query) noexcept
        {
            commandBuffer_.endQuery(queryPool, query, device_.dispatch());
        }

        void endRenderPass() noexcept
        {
            commandBuffer_.endRenderPass(device_.dispatch());
        }

        void executeCommands(vk::ArrayProxy<const vk::CommandBuffer> const& commandBuffers) noexcept
        {
            commandBuffer_.executeCommands(commandBuffers, device_.dispatch());
        }

        void fillBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize size, uint32_t data) noexcept
        {
            commandBuffer_.fillBuffer(dstBuffer, dstOffset, size, data, device_.dispatch());
        }

        void nextSubpass(vk::SubpassContents contents) noexcept
        {
            commandBuffer_.nextSubpass(contents, device_.dispatch());
        }

        void pipelineBarrier(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::DependencyFlags dependencyFlags, vk::ArrayProxy<const vk::MemoryBarrier> const& memoryBarriers, vk::ArrayProxy<const vk::BufferMemoryBarrier> const& bufferMemoryBarriers, vk::ArrayProxy<const vk::ImageMemoryBarrier> const& imageMemoryBarriers) noexcept
        {
            commandBuffer_.pipelineBarrier(srcStageMask, dstStageMask, dependencyFlags, memoryBarriers, bufferMemoryBarriers, imageMemoryBarriers, device_.dispatch());
        }

        void pushConstants(vk::PipelineLayout layout, vk::ShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) noexcept
        {
            commandBuffer_.pushConstants(layout, stageFlags, offset, size, pValues, device_.dispatch());
        }

        void resetEvent(vk::Event event, vk::PipelineStageFlags stageMask) noexcept
        {
            commandBuffer_.resetEvent(event, stageMask, device_.dispatch());
        }

        void resetQueryPool(vk::QueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) noexcept
        {
            commandBuffer_.resetQueryPool(queryPool, firstQuery, queryCount, device_.dispatch());
        }

        void resolveImage(vk::Image srcImage, vk::ImageLayout srcImageLayout, vk::Image dstImage, vk::ImageLayout dstImageLayout, vk::ArrayProxy<const vk::ImageResolve> const& regions) noexcept
        {
            commandBuffer_.resolveImage(srcImage, srcImageLayout, dstImage, dstImageLayout, regions, device_.dispatch());
        }

        void setBlendConstants(const float blendConstants[4]) noexcept
        {
            commandBuffer_.setBlendConstants(blendConstants, device_.dispatch());
        }

        void setDepthBias(float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) noexcept
        {
            commandBuffer_.setDepthBias(depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor, device_.dispatch());
        }

        void setDepthBounds(float minDepthBounds, float maxDepthBounds) noexcept
        {
            commandBuffer_.setDepthBounds(minDepthBounds, maxDepthBounds, device_.dispatch());
        }

        void setEvent(vk::Event event, vk::PipelineStageFlags stageMask) noexcept
        {
            commandBuffer_.setEvent(event, stageMask, device_.dispatch());
        }

        void setLineWidth(float lineWidth) noexcept
        {
            commandBuffer_.setLineWidth(lineWidth, device_.dispatch());
        }

        void setScissor(uint32_t firstScissor, vk::ArrayProxy<const vk::Rect2D> const& scissors) noexcept
        {
            commandBuffer_.setScissor(firstScissor, scissors, device_.dispatch());
        }

        void setStencilCompareMask(vk::StencilFaceFlags faceMask, uint32_t compareMask) noexcept
        {
            commandBuffer_.setStencilCompareMask(faceMask, compareMask, device_.dispatch());
        }

        void setViewport(uint32_t firstViewport, vk::ArrayProxy<const vk::Viewport> const& viewports) noexcept
        {
            commandBuffer_.setViewport(firstViewport, viewports, device_.dispatch());
        }

        void updateBuffer(vk::Buffer dstBuffer, vk::DeviceSize dstOffset, vk::DeviceSize dataSize, const void* pData) noexcept
        {
            commandBuffer_.updateBuffer(dstBuffer, dstOffset, dataSize, pData, device_.dispatch());
        }

        void waitEvents(vk::ArrayProxy<const vk::Event> const& events, vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask, vk::ArrayProxy<const vk::MemoryBarrier> const& memoryBarriers, vk::ArrayProxy<const vk::BufferMemoryBarrier> const& bufferMemoryBarriers, vk::ArrayProxy<const vk::ImageMemoryBarrier> const& imageMemoryBarriers) noexcept
        {
            commandBuffer_.waitEvents(events, srcStageMask, dstStageMask, memoryBarriers, bufferMemoryBarriers, imageMemoryBarriers, device_.dispatch());
        }

        void writeTimestamp(vk::PipelineStageFlagBits pipelineStage, vk::QueryPool queryPool, uint32_t query) noexcept
        {
            commandBuffer_.writeTimestamp(pipelineStage, queryPool, query, device_.dispatch());
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

        Device device() const
        {
            return device_;
        }

        const vk::DispatchLoaderDynamic& dispatch() const
        {
            return device_.dispatch();
        }

        vk::CommandBuffer vkCommandBuffer() const
        {
            return commandBuffer_;
        }

        vk::CommandBuffer vkHandle() const
        {
            return commandBuffer_;
        }

        operator vk::CommandBuffer() const
        {
            return commandBuffer_;
        }

    private:
        explicit CommandBuffer(Device device, vk::CommandBuffer commandBuffer);

        Device device_;
        vk::CommandBuffer commandBuffer_;
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
            createInfo.queueFamilyIndex = family.queueFamilyIndex();

            return create(family.device(), createInfo);
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
            device().allocateCommandBuffers(vkCommandPool(), commandBufferCount, commandBuffers, level, next);
        }

        void freeCommandBuffers(const vk::ArrayProxy<const vk::CommandBuffer>& commandBuffers) const
        {
            device().freeCommandBuffers(vkCommandPool(), commandBuffers);
        }

        void reset(vk::CommandPoolResetFlags flags) const
        {
            device().resetCommandPool(vkCommandPool(), flags);
        }

        ///////////////////////////////////
        // Version 1.1 ////////////////////
        ///////////////////////////////////

#ifdef VK_VERSION_1_1

        void trim(vk::CommandPoolTrimFlags flags) const
        {
            device().trimCommandPool(vkCommandPool(), flags);
        }

#endif

        /////////////////////////////////
        // Extensions ///////////////////
        /////////////////////////////////

#ifdef VK_KHR_MAINTENANCE1_EXTENSION_NAME

        void trimKHR(vk::CommandPoolTrimFlagsKHR flags) const
        {
            device().trimCommandPoolKHR(vkCommandPool(), flags);
        }

#endif

        ///////////////////////////////
        // Retrieve handles ///////////
        ///////////////////////////////

        Device device() const;
        const vk::DispatchLoaderDynamic& dispatch() const;

        vk::CommandPool vkCommandPool() const;
        vk::CommandPool vkHandle() const;
        operator vk::CommandPool() const;

    private:
        explicit CommandPool(Impl* impl);

        Impl* impl_ = nullptr;
    };

} // namespace vkq
