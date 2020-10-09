#include "command_buffer.hpp"
#include "device.hpp"

#include "images/format.hpp"
#include <cstring>

namespace Vulkan
{

	CommandBuffer::CommandBuffer(Device* device_, VkCommandBuffer cmd_, VkPipelineCache cache_, Type type_)
		: device(device_)
		, table(device_->GetDeviceTable())
		, cmd(cmd_)
		, type(type_)
	{
		pipeline_state.cache = cache_;
		BeginCompute();
		SetOpaqueState();
		memset(&pipeline_state.static_state, 0, sizeof(pipeline_state.static_state));
	}

	CommandBuffer::~CommandBuffer()
	{
		VK_ASSERT(vbo_block.mapped     == nullptr);
		VK_ASSERT(ibo_block.mapped     == nullptr);
		VK_ASSERT(ubo_block.mapped     == nullptr);
		VK_ASSERT(staging_block.mapped == nullptr);
	}

	void CommandBuffer::FillBuffer(const Buffer& dst, uint32_t value)
	{
		FillBuffer(dst, value, 0, VK_WHOLE_SIZE);
	}

	void CommandBuffer::FillBuffer(const Buffer& dst, uint32_t value, VkDeviceSize offset, VkDeviceSize size)
	{
		table.vkCmdFillBuffer(cmd, dst.GetBuffer(), offset, size, value);
	}

	void CommandBuffer::CopyBuffer(const Buffer& dst, VkDeviceSize dst_offset, const Buffer& src, VkDeviceSize src_offset,
		VkDeviceSize size)
	{
		const VkBufferCopy region = {
			src_offset, dst_offset, size,
		};
		table.vkCmdCopyBuffer(cmd, src.GetBuffer(), dst.GetBuffer(), 1, &region);
	}

	void CommandBuffer::CopyBuffer(const Buffer& dst, const Buffer& src)
	{
		VK_ASSERT(dst.GetCreateInfo().size == src.GetCreateInfo().size);
		CopyBuffer(dst, 0, src, 0, dst.GetAllocation().size);
	}

	void CommandBuffer::CopyBuffer(const Buffer& dst, const Buffer& src, const VkBufferCopy* copies, size_t count)
	{
		table.vkCmdCopyBuffer(cmd, src.GetBuffer(), dst.GetBuffer(), count, copies);
	}

	void CommandBuffer::CopyImage(const Vulkan::Image& dst, const Vulkan::Image& src, const VkOffset3D& dst_offset,
		const VkOffset3D& src_offset, const VkExtent3D& extent,
		const VkImageSubresourceLayers& dst_subresource,
		const VkImageSubresourceLayers& src_subresource)
	{
		VkImageCopy region = {};
		region.dstOffset = dst_offset;
		region.srcOffset = src_offset;
		region.extent = extent;
		region.srcSubresource = src_subresource;
		region.dstSubresource = dst_subresource;

		table.vkCmdCopyImage(cmd, src.GetImage(), src.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			dst.GetImage(), dst.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
			1, &region);
	}

	void CommandBuffer::CopyImage(const Image& dst, const Image& src)
	{
		uint32_t levels = src.GetCreateInfo().levels;
		VK_ASSERT(src.GetCreateInfo().levels == dst.GetCreateInfo().levels);
		VK_ASSERT(src.GetCreateInfo().width == dst.GetCreateInfo().width);
		VK_ASSERT(src.GetCreateInfo().height == dst.GetCreateInfo().height);
		VK_ASSERT(src.GetCreateInfo().depth == dst.GetCreateInfo().depth);
		VK_ASSERT(src.GetCreateInfo().type == dst.GetCreateInfo().type);
		VK_ASSERT(src.GetCreateInfo().layers == dst.GetCreateInfo().layers);
		VK_ASSERT(src.GetCreateInfo().levels == dst.GetCreateInfo().levels);

		//TODO command buffer owned memory arena?
		VkImageCopy regions[32] = {};

		for (uint32_t i = 0; i < levels; i++)
		{
			auto& region = regions[i];
			region.extent.width = src.GetCreateInfo().width;
			region.extent.height = src.GetCreateInfo().height;
			region.extent.depth = src.GetCreateInfo().depth;
			region.srcSubresource.aspectMask = FormatToAspectMask(src.GetFormat());
			region.srcSubresource.layerCount = src.GetCreateInfo().layers;
			region.dstSubresource.aspectMask = FormatToAspectMask(dst.GetFormat());
			region.dstSubresource.layerCount = dst.GetCreateInfo().layers;
			region.srcSubresource.mipLevel = i;
			region.dstSubresource.mipLevel = i;
			VK_ASSERT(region.srcSubresource.aspectMask == region.dstSubresource.aspectMask);
		}

		table.vkCmdCopyImage(cmd, src.GetImage(), src.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			dst.GetImage(), dst.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
			levels, regions);
	}

	void CommandBuffer::CopyBufferToImage(const Image& image, const Buffer& buffer, unsigned num_blits,
		const VkBufferImageCopy* blits)
	{
		table.vkCmdCopyBufferToImage(cmd, buffer.GetBuffer(),
			image.GetImage(), image.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL), num_blits, blits);
	}

	void CommandBuffer::CopyImageToBuffer(const Buffer& buffer, const Image& image, unsigned num_blits,
		const VkBufferImageCopy* blits)
	{
		table.vkCmdCopyImageToBuffer(cmd, image.GetImage(), image.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			buffer.GetBuffer(), num_blits, blits);
	}

	void CommandBuffer::CopyBufferToImage(const Image& image, const Buffer& src, VkDeviceSize buffer_offset, const VkOffset3D& offset, const VkExtent3D& extent,
		unsigned row_length, unsigned slice_height, const VkImageSubresourceLayers& subresource)
	{
		const VkBufferImageCopy region = {
			buffer_offset,
			row_length, slice_height,
			subresource, offset, extent,
		};
		table.vkCmdCopyBufferToImage(cmd, src.GetBuffer(), image.GetImage(), image.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
			1, &region);
	}

	void CommandBuffer::CopyImageToBuffer(const Buffer& buffer, const Image& image, VkDeviceSize buffer_offset,
		const VkOffset3D& offset, const VkExtent3D& extent, unsigned row_length,
		unsigned slice_height, const VkImageSubresourceLayers& subresource)
	{
		const VkBufferImageCopy region = {
			buffer_offset,
			row_length, slice_height,
			subresource, offset, extent,
		};
		table.vkCmdCopyImageToBuffer(cmd, image.GetImage(), image.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			buffer.GetBuffer(), 1, &region);
	}


	void CommandBuffer::ClearImage(const Image& image, const VkClearValue& value)
	{
		auto aspect = FormatToAspectMask(image.GetFormat());
		ClearImage(image, value, aspect);
	}

	void CommandBuffer::ClearImage(const Image& image, const VkClearValue& value, VkImageAspectFlags aspect)
	{
		VK_ASSERT(!framebuffer);
		VK_ASSERT(!actual_render_pass);

		VkImageSubresourceRange range = {};
		range.aspectMask = aspect;
		range.baseArrayLayer = 0;
		range.baseMipLevel = 0;
		range.levelCount = image.GetCreateInfo().levels;
		range.layerCount = image.GetCreateInfo().layers;
		if (aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
		{
			table.vkCmdClearDepthStencilImage(cmd, image.GetImage(), image.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
				&value.depthStencil, 1, &range);
		}
		else
		{
			table.vkCmdClearColorImage(cmd, image.GetImage(), image.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
				&value.color, 1, &range);
		}
	}

	void CommandBuffer::ClearQuad(unsigned attachment, const VkClearRect& rect, const VkClearValue& value,
		VkImageAspectFlags aspect)
	{
		VK_ASSERT(framebuffer);
		VK_ASSERT(actual_render_pass);
		VkClearAttachment att = {};
		att.clearValue = value;
		att.colorAttachment = attachment;
		att.aspectMask = aspect;
		table.vkCmdClearAttachments(cmd, 1, &att, 1, &rect);
	}

	void CommandBuffer::ClearQuad(const VkClearRect& rect, const VkClearAttachment* attachments, unsigned num_attachments)
	{
		VK_ASSERT(framebuffer);
		VK_ASSERT(actual_render_pass);
		table.vkCmdClearAttachments(cmd, num_attachments, attachments, 1, &rect);
	}

	void CommandBuffer::FullBarrier()
	{
		VK_ASSERT(!actual_render_pass);
		VK_ASSERT(!framebuffer);
		Barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
	}

	void CommandBuffer::PixelBarrier()
	{
		VK_ASSERT(actual_render_pass);
		VK_ASSERT(framebuffer);
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		table.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 1, &barrier, 0, nullptr, 0, nullptr);
	}

	static inline void fixup_src_stage(VkPipelineStageFlags& src_stages, bool fixup)
	{
		// ALL_GRAPHICS_BIT waits for vertex as well which causes performance issues on some drivers.
		// It shouldn't matter, but hey.
		//
		// We aren't using vertex with side-effects on relevant hardware so dropping VERTEX_SHADER_BIT is fine.
		if ((src_stages & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT) != 0 && fixup)
		{
			src_stages &= ~VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
			src_stages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		}
	}

	void CommandBuffer::Barrier(VkPipelineStageFlags src_stages, VkAccessFlags src_access, VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
	{
		VK_ASSERT(!actual_render_pass);
		VK_ASSERT(!framebuffer);
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = src_access;
		barrier.dstAccessMask = dst_access;
		fixup_src_stage(src_stages, device->GetWorkarounds().optimize_all_graphics_barrier);
		table.vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	}

	void CommandBuffer::Barrier(VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages, 
		unsigned barriers, const VkMemoryBarrier* globals, 
		unsigned buffer_barriers, const VkBufferMemoryBarrier* buffers, 
		unsigned image_barriers, const VkImageMemoryBarrier* images)
	{
		VK_ASSERT(!actual_render_pass);
		VK_ASSERT(!framebuffer);
		fixup_src_stage(src_stages, device->GetWorkarounds().optimize_all_graphics_barrier);
		table.vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, barriers, globals, buffer_barriers, buffers, image_barriers, images);
	}

	void CommandBuffer::BufferBarrier(const Buffer& buffer, VkPipelineStageFlags src_stages, VkAccessFlags src_access, VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
	{
		VK_ASSERT(!actual_render_pass);
		VK_ASSERT(!framebuffer);
		VkBufferMemoryBarrier barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		barrier.srcAccessMask = src_access;
		barrier.dstAccessMask = dst_access;
		barrier.buffer = buffer.GetBuffer();
		barrier.offset = 0;
		barrier.size = buffer.GetCreateInfo().size;

		fixup_src_stage(src_stages, device->GetWorkarounds().optimize_all_graphics_barrier);
		table.vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 0, nullptr, 1, &barrier, 0, nullptr);
	}

	void CommandBuffer::ImageBarrier(const Image& image, VkImageLayout old_layout, VkImageLayout new_layout,
		VkPipelineStageFlags src_stages, VkAccessFlags src_access,
		VkPipelineStageFlags dst_stages, VkAccessFlags dst_access)
	{
		VK_ASSERT(!actual_render_pass);
		VK_ASSERT(!framebuffer);
		VK_ASSERT(image.GetCreateInfo().domain != ImageDomain::Transient);

		VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		barrier.srcAccessMask = src_access;
		barrier.dstAccessMask = dst_access;
		barrier.oldLayout = old_layout;
		barrier.newLayout = new_layout;
		barrier.image = image.GetImage();
		barrier.subresourceRange.aspectMask = FormatToAspectMask(image.GetCreateInfo().format);
		barrier.subresourceRange.levelCount = image.GetCreateInfo().levels;
		barrier.subresourceRange.layerCount = image.GetCreateInfo().layers;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		fixup_src_stage(src_stages, device->GetWorkarounds().optimize_all_graphics_barrier);
		table.vkCmdPipelineBarrier(cmd, src_stages, dst_stages, 0, 0, nullptr, 0, nullptr, 1, &barrier);
	}

	void CommandBuffer::WaitEvents(unsigned num_events, const VkEvent* events,
		VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
		unsigned barriers,
		const VkMemoryBarrier* globals, unsigned buffer_barriers,
		const VkBufferMemoryBarrier* buffers, unsigned image_barriers,
		const VkImageMemoryBarrier* images)
	{
		VK_ASSERT(!framebuffer);
		VK_ASSERT(!actual_render_pass);

		if (device->GetWorkarounds().emulate_event_as_pipeline_barrier)
		{
			Barrier(src_stages, dst_stages,
				barriers, globals,
				buffer_barriers, buffers,
				image_barriers, images);
		}
		else
		{
			table.vkCmdWaitEvents(cmd, num_events, events, src_stages, dst_stages,
				barriers, globals, buffer_barriers, buffers, image_barriers, images);
		}
	}

	PipelineEvent CommandBuffer::SignalEvent(VkPipelineStageFlags stages)
	{
		VK_ASSERT(!framebuffer);
		VK_ASSERT(!actual_render_pass);
		auto event = device->RequestPipelineEvent();
		if (!device->GetWorkarounds().emulate_event_as_pipeline_barrier)
			table.vkCmdSetEvent(cmd, event->get_event(), stages);
		event->set_stages(stages);
		return event;
	}

	void CommandBuffer::BlitImage(const Image& dst, const Image& src,
		const VkOffset3D& dst_offset, const VkOffset3D& dst_extent, const VkOffset3D& src_offset, const VkOffset3D& src_extent,
		unsigned dst_level, unsigned src_level, unsigned dst_base_layer, unsigned src_base_layer,
		unsigned num_layers, VkFilter filter)
	{
		const auto add_offset = [](const VkOffset3D& a, const VkOffset3D& b) -> VkOffset3D {
			return { a.x + b.x, a.y + b.y, a.z + b.z };
		};

#if 0
		VkImageBlit blit{};

		blit.srcSubresource.aspectMask = FormatToAspectMask(src.GetCreateInfo().format);
		blit.srcSubresource.mipLevel = src_level;
		blit.srcSubresource.baseArrayLayer = src_base_layer;
		blit.srcSubresource.layerCount = num_layers;
		blit.srcOffsets[0] = src_offset;
		blit.srcOffsets[1] = add_offset(src_offset, src_extent);

		blit.dstSubresource.aspectMask = FormatToAspectMask(dst.GetCreateInfo().format);
		blit.dstSubresource.mipLevel = dst_level;
		blit.dstSubresource.baseArrayLayer = dst_base_layer;
		blit.dstSubresource.layerCount = num_layers;
		blit.dstOffsets[0] = dst_offset;
		blit.dstOffsets[1] = add_offset(dst_offset, dst_extent);

		table.vkCmdBlitImage(cmd,
			src.GetImage(), src.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			dst.GetImage(), dst.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
			1, &blit, filter);
#else
		// RADV workaround.
		for (unsigned i = 0; i < num_layers; i++)
		{
			VkImageBlit blit{};

			blit.srcSubresource.aspectMask = FormatToAspectMask(src.GetCreateInfo().format);
			blit.srcSubresource.mipLevel = src_level;
			blit.srcSubresource.baseArrayLayer = src_base_layer + i;
			blit.srcSubresource.layerCount = 1;
			blit.srcOffsets[0] = src_offset;
			blit.srcOffsets[1] = add_offset(src_offset, src_extent);

			blit.dstSubresource.aspectMask = FormatToAspectMask(dst.GetCreateInfo().format);
			blit.dstSubresource.mipLevel = dst_level;
			blit.dstSubresource.baseArrayLayer = dst_base_layer + i;
			blit.dstSubresource.layerCount = 1;
			blit.dstOffsets[0] = dst_offset;
			blit.dstOffsets[1] = add_offset(dst_offset, dst_extent);


			table.vkCmdBlitImage(cmd,
				src.GetImage(), src.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
				dst.GetImage(), dst.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
				1, &blit, filter);
		}
#endif
	}

	void CommandBuffer::BarrierPrepareGenerateMipmap(const Image& image, VkImageLayout base_level_layout, VkPipelineStageFlags src_stage, VkAccessFlags src_access, bool need_top_level_barrier)
	{
		auto& create_info = image.GetCreateInfo();
		VkImageMemoryBarrier barriers[2] = {};
		VK_ASSERT(create_info.levels > 1);
		(void)create_info;

		for (unsigned i = 0; i < 2; i++)
		{
			barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barriers[i].image = image.GetImage();
			barriers[i].subresourceRange.aspectMask = FormatToAspectMask(image.GetFormat());
			barriers[i].subresourceRange.layerCount = image.GetCreateInfo().layers;
			barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			if (i == 0)
			{
				barriers[i].oldLayout = base_level_layout;
				barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				barriers[i].srcAccessMask = src_access;
				barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				barriers[i].subresourceRange.baseMipLevel = 0;
				barriers[i].subresourceRange.levelCount = 1;
			}
			else
			{
				barriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				barriers[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				barriers[i].srcAccessMask = 0;
				barriers[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barriers[i].subresourceRange.baseMipLevel = 1;
				barriers[i].subresourceRange.levelCount = image.GetCreateInfo().levels - 1;
			}
		}

		Barrier(src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0, nullptr,
			need_top_level_barrier ? 2 : 1,
			need_top_level_barrier ? barriers : barriers + 1);
	}

	void CommandBuffer::GenerateMipmap(const Image& image)
	{
		auto& create_info = image.GetCreateInfo();
		VkOffset3D size = { int(create_info.width), int(create_info.height), int(create_info.depth) };
		const VkOffset3D origin = { 0, 0, 0 };

		VK_ASSERT(image.GetLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		b.image = image.GetImage();
		b.subresourceRange.levelCount = 1;
		b.subresourceRange.layerCount = image.GetCreateInfo().layers;
		b.subresourceRange.aspectMask = FormatToAspectMask(image.GetFormat());
		b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		for (unsigned i = 1; i < create_info.levels; i++)
		{
			VkOffset3D src_size = size;
			size.x = std::max(size.x >> 1, 1);
			size.y = std::max(size.y >> 1, 1);
			size.z = std::max(size.z >> 1, 1);

			BlitImage(image, image, origin, size, origin, src_size, i, i - 1, 0, 0, create_info.layers, VK_FILTER_LINEAR);

			b.subresourceRange.baseMipLevel = i;
			Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 0, nullptr, 1, &b);
		}
	}

	void CommandBuffer::BeginContext()
	{
		dirty = ~0u;
		dirty_sets = ~0u;
		dirty_vbos = ~0u;
		current_pipeline = VK_NULL_HANDLE;
		current_pipeline_layout = VK_NULL_HANDLE;
		current_layout = nullptr;
		pipeline_state.program.Reset();
		memset(&index_state, 0, sizeof(index_state));
		memset(vbo.buffers, 0, sizeof(vbo.buffers));
	}

	void CommandBuffer::BeginCompute()
	{
		is_compute = true;
		BeginContext();
	}

	void CommandBuffer::BeginGraphics()
	{
		is_compute = false;
		BeginContext();
	}

	void CommandBuffer::InitViewportScissor(const RenderPassInfo& info, const Framebuffer* fb)
	{
		VkRect2D rect = info.render_area;
		rect.offset.x = std::min(fb->GetWidth(), uint32_t(rect.offset.x));
		rect.offset.y = std::min(fb->GetHeight(), uint32_t(rect.offset.y));
		rect.extent.width = std::min(fb->GetWidth() - rect.offset.x, rect.extent.width);
		rect.extent.height = std::min(fb->GetHeight() - rect.offset.y, rect.extent.height);

		viewport = { 0.0f, 0.0f, float(fb->GetWidth()), float(fb->GetHeight()), 0.0f, 1.0f };
		scissor = rect;
	}

	CommandBufferHandle CommandBuffer::RequestSecondaryCommandBuffer(Device& device, const RenderPassInfo& info, unsigned thread_index, unsigned subpass)
	{
		auto* fb = &device.RequestFramebuffer(info);
		auto cmd = device.RequestSecondaryCommandBufferForThread(thread_index, fb, subpass);
		cmd->BeginGraphics();

		cmd->framebuffer = fb;
		cmd->pipeline_state.compatible_render_pass = &fb->GetCompatibleRenderPass();
		cmd->actual_render_pass = &device.RequestRenderPass(info, false);

		unsigned i;
		for (i = 0; i < info.num_color_attachments; i++)
			cmd->framebuffer_attachments[i] = info.color_attachments[i].view;
		if (info.depth_stencil.view)
			cmd->framebuffer_attachments[i++] = info.depth_stencil.view;

		cmd->InitViewportScissor(info, fb);
		cmd->pipeline_state.subpass_index = subpass;
		cmd->current_contents = VK_SUBPASS_CONTENTS_INLINE;

		return cmd;
	}

	CommandBufferHandle CommandBuffer::RequestSecondaryCommandBuffer(unsigned thread_index_, unsigned subpass_)
	{
		VK_ASSERT(framebuffer);
		VK_ASSERT(!is_secondary);

		auto secondary_cmd = device->RequestSecondaryCommandBufferForThread(thread_index_, framebuffer, subpass_);
		secondary_cmd->BeginGraphics();

		secondary_cmd->framebuffer = framebuffer;
		secondary_cmd->pipeline_state.compatible_render_pass = pipeline_state.compatible_render_pass;
		secondary_cmd->actual_render_pass = actual_render_pass;
		memcpy(secondary_cmd->framebuffer_attachments, framebuffer_attachments, sizeof(framebuffer_attachments));

		secondary_cmd->pipeline_state.subpass_index = subpass_;
		secondary_cmd->viewport = viewport;
		secondary_cmd->scissor = scissor;
		secondary_cmd->current_contents = VK_SUBPASS_CONTENTS_INLINE;

		return secondary_cmd;
	}

	void CommandBuffer::SubmitSecondary(Util::IntrusivePtr<CommandBuffer> secondary)
	{
		VK_ASSERT(!is_secondary);
		VK_ASSERT(secondary->is_secondary);
		VK_ASSERT(pipeline_state.subpass_index == secondary->pipeline_state.subpass_index);
		VK_ASSERT(current_contents == VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

		device->SubmitSecondary(*this, *secondary);
	}

	void CommandBuffer::NextSubpass(VkSubpassContents contents)
	{
		VK_ASSERT(framebuffer);
		VK_ASSERT(pipeline_state.compatible_render_pass);
		VK_ASSERT(actual_render_pass);
		pipeline_state.subpass_index++;
		VK_ASSERT(pipeline_state.subpass_index < actual_render_pass->GetNumSubpasses());
		table.vkCmdNextSubpass(cmd, contents);
		current_contents = contents;
		BeginGraphics();
	}

	void CommandBuffer::BeginRenderPass(const RenderPassInfo& info, VkSubpassContents contents)
	{
		VK_ASSERT(!framebuffer);
		VK_ASSERT(!pipeline_state.compatible_render_pass);
		VK_ASSERT(!actual_render_pass);

		framebuffer = &device->RequestFramebuffer(info);
		pipeline_state.compatible_render_pass = &framebuffer->GetCompatibleRenderPass();
		actual_render_pass = &device->RequestRenderPass(info, false);
		pipeline_state.subpass_index = 0;

		memset(framebuffer_attachments, 0, sizeof(framebuffer_attachments));
		unsigned att;
		for (att = 0; att < info.num_color_attachments; att++)
			framebuffer_attachments[att] = info.color_attachments[att].view;
		if (info.depth_stencil.view)
			framebuffer_attachments[att++] = info.depth_stencil.view;

		InitViewportScissor(info, framebuffer);

		VkClearValue clear_values[VULKAN_NUM_ATTACHMENTS + 1];
		unsigned num_clear_values = 0;

		for (unsigned i = 0; i < info.num_color_attachments; i++)
		{
			VK_ASSERT(info.color_attachments[i].view);
			if (info.clear_attachments & (1u << i))
			{
				clear_values[i].color = info.color_attachments[i].clear_color;
				num_clear_values = i + 1;
			}

			if (info.color_attachments[i].view->GetImage().IsSwapchainImage())
				uses_swapchain = true;
		}

		if (info.depth_stencil.view && (info.op_flags & RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT) != 0)
		{
			clear_values[info.num_color_attachments].depthStencil = info.depth_stencil.clear_value;
			num_clear_values = info.num_color_attachments + 1;
		}

		VkRenderPassBeginInfo begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		VkRenderPassAttachmentBeginInfoKHR attachment_info = { VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO_KHR };
		begin_info.renderPass = actual_render_pass->GetRenderPass();
		begin_info.framebuffer = framebuffer->GetFramebuffer();
		begin_info.renderArea = scissor;
		begin_info.clearValueCount = num_clear_values;
		begin_info.pClearValues = clear_values;

		auto& features = device->GetDeviceExtensions();
		bool imageless = features.imageless_features.imagelessFramebuffer == VK_TRUE;
		VkImageView immediate_views[VULKAN_NUM_ATTACHMENTS + 1];
		if (imageless)
		{
			attachment_info.attachmentCount = Framebuffer::SetupRawViews(immediate_views, info);
			attachment_info.pAttachments = immediate_views;
			begin_info.pNext = &attachment_info;
		}

		table.vkCmdBeginRenderPass(cmd, &begin_info, contents);

		current_contents = contents;
		BeginGraphics();
	}

	void CommandBuffer::EndRenderPass()
	{
		VK_ASSERT(framebuffer);
		VK_ASSERT(actual_render_pass);
		VK_ASSERT(pipeline_state.compatible_render_pass);

		table.vkCmdEndRenderPass(cmd);

		framebuffer = nullptr;
		actual_render_pass = nullptr;
		pipeline_state.compatible_render_pass = nullptr;
		BeginCompute();
	}

	VkPipeline CommandBuffer::BuildComputePipeline(Device* device, DeferredPipelineCompile& compile)
	{
		VK_ASSERT(compile.program->HasShader(ShaderStage::Compute));

		auto& shader = *compile.program->GetShader(ShaderStage::Compute);
		VkComputePipelineCreateInfo info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		info.layout = compile.program->GetLayout().GetVkLayout();
		info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		info.stage.module = shader.GetModule();
		info.stage.pName = "main";
		info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;

		VkSpecializationInfo spec_info = {};
		VkSpecializationMapEntry spec_entries[VULKAN_NUM_SPEC_CONSTANTS];
		auto mask = compile.program->GetLayout().GetCombindedSpecConstantMask() & compile.potential_static_state.spec_constant_mask;

		uint32_t spec_constants[VULKAN_NUM_SPEC_CONSTANTS];

		if (mask)
		{
			info.stage.pSpecializationInfo = &spec_info;
			spec_info.pData = spec_constants;
			spec_info.pMapEntries = spec_entries;

			Util::for_each_bit(mask, [&](uint32_t bit) {
				auto& entry = spec_entries[spec_info.mapEntryCount];
				entry.offset = sizeof(uint32_t) * spec_info.mapEntryCount;
				entry.size = sizeof(uint32_t);
				entry.constantID = bit;

				spec_constants[spec_info.mapEntryCount] = compile.potential_static_state.spec_constants[bit];
				spec_info.mapEntryCount++;
				});
			spec_info.dataSize = spec_info.mapEntryCount * sizeof(uint32_t);
		}

		VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT subgroup_size_info = {
			VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT
		};

		if (compile.static_state.state.subgroup_control_size)
		{
			auto& features = device->GetDeviceExtensions();

			if (!features.subgroup_size_control_features.subgroupSizeControl)
			{
				QM_LOG_ERROR("Device does not support subgroup size control.\n");
				return VK_NULL_HANDLE;
			}

			if (compile.static_state.state.subgroup_full_group)
			{
				if (!features.subgroup_size_control_features.computeFullSubgroups)
				{
					QM_LOG_ERROR("Device does not support full subgroups.\n");
					return VK_NULL_HANDLE;
				}

				info.stage.flags |= VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT;
			}

			uint32_t min_subgroups = 1u << compile.static_state.state.subgroup_minimum_size_log2;
			uint32_t max_subgroups = 1u << compile.static_state.state.subgroup_maximum_size_log2;
			if (min_subgroups <= features.subgroup_size_control_properties.minSubgroupSize &&
				max_subgroups >= features.subgroup_size_control_properties.maxSubgroupSize)
			{
				info.stage.flags |= VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT;
			}
			else
			{
				// Pick a fixed subgroup size. Prefer smallest subgroup size.
				if (min_subgroups < features.subgroup_size_control_properties.minSubgroupSize)
					subgroup_size_info.requiredSubgroupSize = features.subgroup_size_control_properties.minSubgroupSize;
				else
					subgroup_size_info.requiredSubgroupSize = min_subgroups;

				info.stage.pNext = &subgroup_size_info;

				if (subgroup_size_info.requiredSubgroupSize < features.subgroup_size_control_properties.minSubgroupSize ||
					subgroup_size_info.requiredSubgroupSize > features.subgroup_size_control_properties.maxSubgroupSize)
				{
					QM_LOG_ERROR("Requested subgroup size is out of range.\n");
					return VK_NULL_HANDLE;
				}

				if ((features.subgroup_size_control_properties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) == 0)
				{
					QM_LOG_ERROR("Cannot request specific subgroup size in compute.\n");
					return VK_NULL_HANDLE;
				}
			}
		}

		VkPipeline compute_pipeline;

#ifdef VULKAN_DEBUG
		QM_LOG_INFO("Creating compute pipeline.\n");
#endif
		auto& table = device->GetDeviceTable();
		if (table.vkCreateComputePipelines(device->GetDevice(), compile.cache, 1, &info, nullptr, &compute_pipeline) != VK_SUCCESS)
		{
			QM_LOG_ERROR("Failed to create compute pipeline!\n");
			return VK_NULL_HANDLE;
		}

		return compile.program->AddPipeline(compile.hash, compute_pipeline);
	}

	void CommandBuffer::ExtractPipelineState(DeferredPipelineCompile& compile) const
	{
		compile = pipeline_state;

		if (!compile.program)
		{
			QM_LOG_ERROR("Attempting to extract pipeline state when no program is bound.\n");
			return;
		}

		if (is_compute)
			UpdateHashComputePipeline(compile);
		else
		{
			uint32_t active_vbo = 0;
			UpdateHashGraphicsPipeline(compile, active_vbo);
		}
	}

	VkPipeline CommandBuffer::BuildGraphicsPipeline(Device* device, DeferredPipelineCompile& compile)
	{
		// Viewport state
		VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		vp.viewportCount = 1;
		vp.scissorCount = 1;

		// Dynamic state
		VkPipelineDynamicStateCreateInfo dyn = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dyn.dynamicStateCount = 2;
		VkDynamicState states[7] = {
			VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT,
		};
		dyn.pDynamicStates = states;

		if (compile.static_state.state.depth_bias_enable)
			states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BIAS;
		if (compile.static_state.state.stencil_test)
		{
			states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
			states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
			states[dyn.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
		}

		// Blend state
		VkPipelineColorBlendAttachmentState blend_attachments[VULKAN_NUM_ATTACHMENTS];
		VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		blend.attachmentCount = compile.compatible_render_pass->GetNumColorAttachments(compile.subpass_index);
		blend.pAttachments = blend_attachments;
		for (unsigned i = 0; i < blend.attachmentCount; i++)
		{
			auto& att = blend_attachments[i];
			att = {};

			if (compile.compatible_render_pass->GetColorAttachment(compile.subpass_index, i).attachment != VK_ATTACHMENT_UNUSED && (compile.program->GetLayout().GetRenderTargetMask() & (1u << i)))
			{
				att.colorWriteMask = (compile.static_state.state.write_mask >> (4 * i)) & 0xf;
				att.blendEnable = compile.static_state.state.blend_enable;
				if (att.blendEnable)
				{
					att.alphaBlendOp = static_cast<VkBlendOp>(compile.static_state.state.alpha_blend_op);
					att.colorBlendOp = static_cast<VkBlendOp>(compile.static_state.state.color_blend_op);
					att.dstAlphaBlendFactor = static_cast<VkBlendFactor>(compile.static_state.state.dst_alpha_blend);
					att.srcAlphaBlendFactor = static_cast<VkBlendFactor>(compile.static_state.state.src_alpha_blend);
					att.dstColorBlendFactor = static_cast<VkBlendFactor>(compile.static_state.state.dst_color_blend);
					att.srcColorBlendFactor = static_cast<VkBlendFactor>(compile.static_state.state.src_color_blend);
				}
			}
		}
		memcpy(blend.blendConstants, compile.potential_static_state.blend_constants, sizeof(blend.blendConstants));

		// Depth state
		VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		ds.stencilTestEnable = compile.compatible_render_pass->HasStencil(compile.subpass_index) && compile.static_state.state.stencil_test;
		ds.depthTestEnable = compile.compatible_render_pass->HasDepth(compile.subpass_index) && compile.static_state.state.depth_test;
		ds.depthWriteEnable = compile.compatible_render_pass->HasDepth(compile.subpass_index) && compile.static_state.state.depth_write;

		if (ds.depthTestEnable)
			ds.depthCompareOp = static_cast<VkCompareOp>(compile.static_state.state.depth_compare);

		if (ds.stencilTestEnable)
		{
			ds.front.compareOp = static_cast<VkCompareOp>(compile.static_state.state.stencil_front_compare_op);
			ds.front.passOp = static_cast<VkStencilOp>(compile.static_state.state.stencil_front_pass);
			ds.front.failOp = static_cast<VkStencilOp>(compile.static_state.state.stencil_front_fail);
			ds.front.depthFailOp = static_cast<VkStencilOp>(compile.static_state.state.stencil_front_depth_fail);
			ds.back.compareOp = static_cast<VkCompareOp>(compile.static_state.state.stencil_back_compare_op);
			ds.back.passOp = static_cast<VkStencilOp>(compile.static_state.state.stencil_back_pass);
			ds.back.failOp = static_cast<VkStencilOp>(compile.static_state.state.stencil_back_fail);
			ds.back.depthFailOp = static_cast<VkStencilOp>(compile.static_state.state.stencil_back_depth_fail);
		}

		// Vertex input
		VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		VkVertexInputAttributeDescription vi_attribs[VULKAN_NUM_VERTEX_ATTRIBS];
		vi.pVertexAttributeDescriptions = vi_attribs;
		uint32_t attr_mask = compile.program->GetLayout().GetAttribMask();
		uint32_t binding_mask = 0;
		Util::for_each_bit(attr_mask, [&](uint32_t bit) {
			auto& attr = vi_attribs[vi.vertexAttributeDescriptionCount++];
			attr.location = bit;
			attr.binding = compile.attribs[bit].binding;
			attr.format = compile.attribs[bit].format;
			attr.offset = compile.attribs[bit].offset;
			binding_mask |= 1u << attr.binding;
			});

		VkVertexInputBindingDescription vi_bindings[VULKAN_NUM_VERTEX_BUFFERS];
		vi.pVertexBindingDescriptions = vi_bindings;
		Util::for_each_bit(binding_mask, [&](uint32_t bit) {
			auto& bind = vi_bindings[vi.vertexBindingDescriptionCount++];
			bind.binding = bit;
			bind.inputRate = compile.input_rates[bit];
			bind.stride = compile.strides[bit];
			});

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		ia.primitiveRestartEnable = compile.static_state.state.primitive_restart;
		ia.topology = static_cast<VkPrimitiveTopology>(compile.static_state.state.topology);

		// Multisample
		VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		ms.rasterizationSamples = static_cast<VkSampleCountFlagBits>(compile.compatible_render_pass->GetSampleCount(compile.subpass_index));

		if (compile.compatible_render_pass->GetSampleCount(compile.subpass_index) > 1)
		{
			ms.alphaToCoverageEnable = compile.static_state.state.alpha_to_coverage;
			ms.alphaToOneEnable = compile.static_state.state.alpha_to_one;
			ms.sampleShadingEnable = compile.static_state.state.sample_shading;
			ms.minSampleShading = 1.0f;
		}

		// Raster
		VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		raster.cullMode = static_cast<VkCullModeFlags>(compile.static_state.state.cull_mode);
		raster.frontFace = static_cast<VkFrontFace>(compile.static_state.state.front_face);
		raster.lineWidth = 1.0f;
		raster.polygonMode = compile.static_state.state.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
		raster.depthBiasEnable = compile.static_state.state.depth_bias_enable != 0;

		VkPipelineRasterizationConservativeStateCreateInfoEXT conservative_raster = {
			VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT
		};
		if (compile.static_state.state.conservative_raster)
		{
			if (device->GetDeviceExtensions().supports_conservative_rasterization)
			{
				raster.pNext = &conservative_raster;
				conservative_raster.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
			}
			else
			{
				QM_LOG_ERROR("Conservative rasterization is not supported on this device.\n");
				return VK_NULL_HANDLE;
			}
		}

		// Tessellation
		VkPipelineTessellationStateCreateInfo tessel = { VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO };
		tessel.flags = 0;
		tessel.patchControlPoints = static_cast<uint32_t>(compile.static_state.state.patch_control_points);
		
		VkPipelineTessellationDomainOriginStateCreateInfo domain_origin = { VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO };
		if (static_cast<VkTessellationDomainOrigin>(compile.static_state.state.domain_origin) != VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT)
		{
			if (device->GetDeviceExtensions().supports_maintenance_2)
			{
				tessel.pNext = &domain_origin;
				domain_origin.domainOrigin = static_cast<VkTessellationDomainOrigin>(compile.static_state.state.domain_origin);
			}
			else
			{
				QM_LOG_ERROR("KHR Maintenance 2 is not supported on this device.\n");
				return VK_NULL_HANDLE;
			}
		}
		
		// Stages
		VkPipelineShaderStageCreateInfo stages[static_cast<unsigned>(ShaderStage::Count)];
		unsigned num_stages = 0;

		VkSpecializationInfo spec_info[Util::ecast(ShaderStage::Count)] = {};
		VkSpecializationMapEntry spec_entries[Util::ecast(ShaderStage::Count)][VULKAN_NUM_SPEC_CONSTANTS];
		uint32_t spec_constants[static_cast<unsigned>(ShaderStage::Count)][VULKAN_NUM_SPEC_CONSTANTS];

		for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
		{
			auto stage = static_cast<ShaderStage>(i);
			if (!compile.program->HasShader(stage))
				continue;

			auto& s = stages[num_stages++];
			s = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			s.module = compile.program->GetShader(stage)->GetModule();
			s.pName = "main";
			s.stage = static_cast<VkShaderStageFlagBits>(1u << i);

			auto mask = compile.program->GetLayout().GetSpecConstantMask(stage) & compile.potential_static_state.spec_constant_mask;

			if (mask)
			{
				s.pSpecializationInfo = &spec_info[i];
				spec_info[i].pData = spec_constants[i];
				spec_info[i].pMapEntries = spec_entries[i];

				Util::for_each_bit(mask, [&](uint32_t bit) {
					auto& entry = spec_entries[i][spec_info[i].mapEntryCount];
					entry.offset = sizeof(uint32_t) * spec_info[i].mapEntryCount;
					entry.size = sizeof(uint32_t);
					entry.constantID = bit;
					spec_constants[i][spec_info[i].mapEntryCount] = compile.potential_static_state.spec_constants[bit];
					spec_info[i].mapEntryCount++;
					});
				spec_info[i].dataSize = spec_info[i].mapEntryCount * sizeof(uint32_t);
			}
		}

		VkGraphicsPipelineCreateInfo pipe = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipe.layout = compile.program->GetLayout().GetVkLayout();
		pipe.renderPass = compile.compatible_render_pass->GetRenderPass();
		pipe.subpass = compile.subpass_index;

		pipe.pViewportState = &vp;
		pipe.pDynamicState = &dyn;
		pipe.pColorBlendState = &blend;
		pipe.pDepthStencilState = &ds;
		pipe.pVertexInputState = &vi;
		pipe.pInputAssemblyState = &ia;
		pipe.pMultisampleState = &ms;
		pipe.pRasterizationState = &raster;
		pipe.pTessellationState = &tessel;
		pipe.pStages = stages;
		pipe.stageCount = num_stages;

		VkPipeline pipeline;

#ifdef VULKAN_DEBUG
		QM_LOG_INFO("Creating graphics pipeline.\n");
#endif
		auto& table = device->GetDeviceTable();
		VkResult res = table.vkCreateGraphicsPipelines(device->GetDevice(), compile.cache, 1, &pipe, nullptr, &pipeline);
		if (res != VK_SUCCESS)
		{
			QM_LOG_ERROR("Failed to create graphics pipeline!\n");
			return VK_NULL_HANDLE;
		}

		return compile.program->AddPipeline(compile.hash, pipeline);
	}

	bool CommandBuffer::FlushComputePipeline(bool synchronous)
	{
		UpdateHashComputePipeline(pipeline_state);
		current_pipeline = pipeline_state.program->GetPipeline(pipeline_state.hash);
		if (current_pipeline == VK_NULL_HANDLE && synchronous)
			current_pipeline = BuildComputePipeline(device, pipeline_state);

		return current_pipeline != VK_NULL_HANDLE;
	}

	bool CommandBuffer::FlushGraphicsPipeline(bool synchronous)
	{
		VK_ASSERT(current_layout);

		UpdateHashGraphicsPipeline(pipeline_state, active_vbos);
		current_pipeline = pipeline_state.program->GetPipeline(pipeline_state.hash);

		if (current_pipeline == VK_NULL_HANDLE && synchronous)
			current_pipeline = BuildGraphicsPipeline(device, pipeline_state);

		return current_pipeline != VK_NULL_HANDLE;
	}

	void CommandBuffer::UpdateHashComputePipeline(DeferredPipelineCompile& compile)
	{
		Util::Hasher h;
		h.u64(compile.program->GetHash());

		// Spec constants.
		uint32_t combined_spec_constant = compile.program->GetLayout().GetCombindedSpecConstantMask();
		combined_spec_constant &= compile.potential_static_state.spec_constant_mask;
		h.u32(combined_spec_constant);
		Util::for_each_bit(combined_spec_constant, [&](uint32_t bit) {
			h.u32(compile.potential_static_state.spec_constants[bit]);
			});

		if (compile.static_state.state.subgroup_control_size)
		{
			h.s32(1);
			h.u32(compile.static_state.state.subgroup_minimum_size_log2);
			h.u32(compile.static_state.state.subgroup_maximum_size_log2);
			h.s32(compile.static_state.state.subgroup_full_group);
		}
		else
			h.s32(0);

		compile.hash = h.get();
	}

	void CommandBuffer::UpdateHashGraphicsPipeline(DeferredPipelineCompile& compile, uint32_t& active_vbos)
	{
		Util::Hasher h;
		active_vbos = 0;
		Util::for_each_bit(compile.program->GetLayout().GetAttribMask(), [&](uint32_t bit) {
			h.u32(bit);
			active_vbos |= 1u << compile.attribs[bit].binding;
			h.u32(compile.attribs[bit].binding);
			h.u32(compile.attribs[bit].format);
			h.u32(compile.attribs[bit].offset);
			});

		Util::for_each_bit(active_vbos, [&](uint32_t bit) {
			h.u32(compile.input_rates[bit]);
			h.u32(compile.strides[bit]);
			});

		h.u64(compile.compatible_render_pass->get_hash());
		h.u32(compile.subpass_index);
		h.u64(compile.program->GetHash());
		h.data(compile.static_state.words, sizeof(compile.static_state.words));

		if (compile.static_state.state.blend_enable)
		{
			const auto needs_blend_constant = [](VkBlendFactor factor) {
				return factor == VK_BLEND_FACTOR_CONSTANT_COLOR || factor == VK_BLEND_FACTOR_CONSTANT_ALPHA;
			};
			bool b0 = needs_blend_constant(static_cast<VkBlendFactor>(compile.static_state.state.src_color_blend));
			bool b1 = needs_blend_constant(static_cast<VkBlendFactor>(compile.static_state.state.src_alpha_blend));
			bool b2 = needs_blend_constant(static_cast<VkBlendFactor>(compile.static_state.state.dst_color_blend));
			bool b3 = needs_blend_constant(static_cast<VkBlendFactor>(compile.static_state.state.dst_alpha_blend));
			if (b0 || b1 || b2 || b3)
				h.data(reinterpret_cast<const uint32_t*>(compile.potential_static_state.blend_constants), sizeof(compile.potential_static_state.blend_constants));
		}

		// Spec constants.
		uint32_t combined_spec_constant = compile.program->GetLayout().GetCombindedSpecConstantMask();
		combined_spec_constant &= compile.potential_static_state.spec_constant_mask;
		h.u32(combined_spec_constant);
		Util::for_each_bit(combined_spec_constant, [&](uint32_t bit) {
			h.u32(compile.potential_static_state.spec_constants[bit]);
			});

		compile.hash = h.get();
	}

	bool CommandBuffer::FlushComputeState(bool synchronous)
	{
		if (!pipeline_state.program)
			return false;
		VK_ASSERT(current_layout);

		if (current_pipeline == VK_NULL_HANDLE)
			set_dirty(COMMAND_BUFFER_DIRTY_PIPELINE_BIT);

		if (GetAndClear(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT | COMMAND_BUFFER_DIRTY_PIPELINE_BIT))
		{
			VkPipeline old_pipe = current_pipeline;
			if (!FlushComputePipeline(synchronous))
				return false;

			if (old_pipe != current_pipeline)
				table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, current_pipeline);
		}

		if (current_pipeline == VK_NULL_HANDLE)
			return false;

		FlushDescriptorSets();

		if (GetAndClear(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT))
		{
			auto& range = current_layout->GetPushConstantRange();
			if (range.stageFlags != 0)
			{
				VK_ASSERT(range.offset == 0);
				table.vkCmdPushConstants(cmd, current_pipeline_layout, range.stageFlags, 0, range.size, push_constant_data);
			}
		}

		return true;
	}

	bool CommandBuffer::FlushRenderState(bool synchronous)
	{
		if (!pipeline_state.program)
			return false;
		VK_ASSERT(current_layout);

		if (current_pipeline == VK_NULL_HANDLE)
			set_dirty(COMMAND_BUFFER_DIRTY_PIPELINE_BIT);

		// We've invalidated pipeline state, update the VkPipeline.
		if (GetAndClear(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT | COMMAND_BUFFER_DIRTY_PIPELINE_BIT | COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT))
		{
			VkPipeline old_pipe = current_pipeline;
			if (!FlushGraphicsPipeline(synchronous))
				return false;

			if (old_pipe != current_pipeline)
			{
				table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, current_pipeline);
				set_dirty(COMMAND_BUFFER_DYNAMIC_BITS);
			}
		}

		if (current_pipeline == VK_NULL_HANDLE)
			return false;

		FlushDescriptorSets();

		if (GetAndClear(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT))
		{
			auto& range = current_layout->GetPushConstantRange();
			if (range.stageFlags != 0)
			{
				VK_ASSERT(range.offset == 0);
				table.vkCmdPushConstants(cmd, current_pipeline_layout, range.stageFlags, 0, range.size, push_constant_data);
			}
		}

		if (GetAndClear(COMMAND_BUFFER_DIRTY_VIEWPORT_BIT))
			table.vkCmdSetViewport(cmd, 0, 1, &viewport);
		if (GetAndClear(COMMAND_BUFFER_DIRTY_SCISSOR_BIT))
			table.vkCmdSetScissor(cmd, 0, 1, &scissor);
		if (pipeline_state.static_state.state.depth_bias_enable && GetAndClear(COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT))
			table.vkCmdSetDepthBias(cmd, dynamic_state.depth_bias_constant, 0.0f, dynamic_state.depth_bias_slope);
		if (pipeline_state.static_state.state.stencil_test && GetAndClear(COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT))
		{
			table.vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_BIT, dynamic_state.front_compare_mask);
			table.vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, dynamic_state.front_reference);
			table.vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_BIT, dynamic_state.front_write_mask);
			table.vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_BACK_BIT, dynamic_state.back_compare_mask);
			table.vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_BACK_BIT, dynamic_state.back_reference);
			table.vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_BACK_BIT, dynamic_state.back_write_mask);
		}

		uint32_t update_vbo_mask = dirty_vbos & active_vbos;
		Util::for_each_bit_range(update_vbo_mask, [&](uint32_t binding, uint32_t binding_count) {
#ifdef VULKAN_DEBUG
			for (unsigned i = binding; i < binding + binding_count; i++)
				VK_ASSERT(vbo.buffers[i] != VK_NULL_HANDLE);
#endif
			table.vkCmdBindVertexBuffers(cmd, binding, binding_count, vbo.buffers + binding, vbo.offsets + binding);
			});
		dirty_vbos &= ~update_vbo_mask;

		return true;
	}

	bool CommandBuffer::FlushPipelineStateWithoutBlocking()
	{
		if (is_compute)
			return FlushComputeState(false);
		else
			return FlushRenderState(false);
	}

	void CommandBuffer::SetVertexAttrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset)
	{
		//Asset that attrib is less than VULKAN_NUM_VERTEX_ATTRIBS
		VK_ASSERT(attrib < VULKAN_NUM_VERTEX_ATTRIBS);
		//And that is is called from within a renderpass
		VK_ASSERT(framebuffer);

		auto& attr = pipeline_state.attribs[attrib];

		//If the attribute is different
		if (attr.binding != binding || attr.format != format || attr.offset != offset)
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT); //Indicate that it has changed

		VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);

		attr.binding = binding;
		attr.format = format;
		attr.offset = offset;
	}

	void CommandBuffer::BindIndexBuffer(const Buffer& buffer, VkDeviceSize offset, VkIndexType index_type)
	{
		//If index buffer is already set to this, return
		if (index_state.buffer == buffer.GetBuffer() && index_state.offset == offset && index_state.index_type == index_type)
			return;

		index_state.buffer = buffer.GetBuffer();
		index_state.offset = offset;
		index_state.index_type = index_type;
		//Bind the index buffer
		table.vkCmdBindIndexBuffer(cmd, buffer.GetBuffer(), offset, index_type);
	}

	void CommandBuffer::SetVertexBinding(uint32_t binding, VkDeviceSize stride, VkVertexInputRate step_rate)
	{
		VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);
		VK_ASSERT(framebuffer);

		if (pipeline_state.strides[binding] != stride || pipeline_state.input_rates[binding] != step_rate)
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT); //If stride or step_rate changes, indicate that the whole vertex_state is dirty.

		pipeline_state.strides[binding] = stride;
		pipeline_state.input_rates[binding] = step_rate;
	}

	void CommandBuffer::BindVertexBuffer(uint32_t binding, const Buffer& buffer, VkDeviceSize offset)
	{
		VK_ASSERT(binding < VULKAN_NUM_VERTEX_BUFFERS);
		VK_ASSERT(framebuffer);

		//Retrive the vkBuffer
		VkBuffer vkbuffer = buffer.GetBuffer();
		if (vbo.buffers[binding] != vkbuffer || vbo.offsets[binding] != offset)
			dirty_vbos |= 1u << binding; //Indicate wich bindings in the vbo are now dirty

		vbo.buffers[binding] = vkbuffer;
		vbo.offsets[binding] = offset;
	}

	void CommandBuffer::SetViewport(const VkViewport& viewport_)
	{
		VK_ASSERT(framebuffer);
		viewport = viewport_;
		//Indicate that the viewport has changed
		set_dirty(COMMAND_BUFFER_DIRTY_VIEWPORT_BIT);
	}

	const VkViewport& CommandBuffer::GetViewport() const
	{
		return viewport;
	}

	void CommandBuffer::SetScissor(const VkRect2D& rect)
	{
		VK_ASSERT(framebuffer);
		VK_ASSERT(rect.offset.x >= 0);
		VK_ASSERT(rect.offset.y >= 0);
		scissor = rect;
		set_dirty(COMMAND_BUFFER_DIRTY_SCISSOR_BIT);
	}

	void CommandBuffer::PushConstants(const void* data, VkDeviceSize offset, VkDeviceSize range)
	{
		VK_ASSERT(offset + range <= VULKAN_PUSH_CONSTANT_SIZE);
		VK_ASSERT(current_layout);
		memcpy(push_constant_data + offset, data, range);
		set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);
	}

	void CommandBuffer::SetProgram(ProgramHandle& program)
	{
		//If this is already set as the program, ignore this call
		if (pipeline_state.program == program)
			return;

		//Otherwise set the current program to program
		pipeline_state.program = program;
		current_pipeline = VK_NULL_HANDLE;
		//And indicate that the pipeline and dynamic state have changed
		set_dirty(COMMAND_BUFFER_DIRTY_PIPELINE_BIT | COMMAND_BUFFER_DYNAMIC_BITS);
		if (!program)
			return;
		//Make sure there is at least either a Compute or Vertex shader
		VK_ASSERT((framebuffer && pipeline_state.program->HasShader(ShaderStage::Vertex)) || (!framebuffer && pipeline_state.program->HasShader(ShaderStage::Compute)));

		program->ResetUniforms();

		//Indicate that all sets must be changed
		dirty_sets = ~0u;
		//As well as the push constants
		set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);
		//Set the layout
		current_layout = &program->GetLayout();
		current_pipeline_layout = current_layout->GetVkLayout();
	}

	void* CommandBuffer::AllocateConstantData(unsigned set, unsigned binding, unsigned array_index, VkDeviceSize size)
	{
		VK_ASSERT(size <= VULKAN_MAX_UBO_SIZE);
		auto data = ubo_block.Allocate(size);
		if (!data.host)
		{
			device->RequestUniformBlock(ubo_block, size);
			data = ubo_block.Allocate(size);
		}
		SetUniformBuffer(set, binding, array_index,  *ubo_block.gpu, data.offset, data.padded_size);
		return data.host;
	}

	void* CommandBuffer::AllocateIndexData(VkDeviceSize size, VkIndexType index_type)
	{
		auto data = ibo_block.Allocate(size);
		if (!data.host)
		{
			device->RequestIndexBlock(ibo_block, size);
			data = ibo_block.Allocate(size);
		}
		BindIndexBuffer(*ibo_block.gpu, data.offset, index_type);
		return data.host;
	}

	void* CommandBuffer::AllocateVertexData(unsigned binding, VkDeviceSize size)
	{
		auto data = vbo_block.Allocate(size);
		if (!data.host)
		{
			device->RequestVertexBlock(vbo_block, size);
			data = vbo_block.Allocate(size);
		}

		BindVertexBuffer(binding, *vbo_block.gpu, data.offset);
		return data.host;
	}

	void* CommandBuffer::UpdateBuffer(const Buffer& buffer, VkDeviceSize offset, VkDeviceSize size)
	{
		if (size == 0)
			return nullptr;

		auto data = staging_block.Allocate(size);
		if (!data.host)
		{
			device->RequestStagingBlock(staging_block, size);
			data = staging_block.Allocate(size);
		}
		CopyBuffer(buffer, offset, *staging_block.cpu, data.offset, size);
		return data.host;
	}

	void* CommandBuffer::UpdateImage(const Image& image, const VkOffset3D& offset, const VkExtent3D& extent, uint32_t row_length, uint32_t image_height, const VkImageSubresourceLayers& subresource)
	{
		auto& create_info = image.GetCreateInfo();
		uint32_t width = std::max(image.GetWidth() >> subresource.mipLevel, 1u);
		uint32_t height = std::max(image.GetHeight() >> subresource.mipLevel, 1u);
		uint32_t depth = std::max(image.GetDepth() >> subresource.mipLevel, 1u);

		if (!row_length)
			row_length = width;

		if (!image_height)
			image_height = height;

		uint32_t blocks_x = row_length;
		uint32_t blocks_y = image_height;
		format_num_blocks(create_info.format, blocks_x, blocks_y);

		VkDeviceSize size = TextureFormatLayout::FormatBlockSize(create_info.format, subresource.aspectMask) * subresource.layerCount * depth * blocks_x * blocks_y;

		auto data = staging_block.Allocate(size);
		if (!data.host)
		{
			device->RequestStagingBlock(staging_block, size);
			data = staging_block.Allocate(size);
		}

		CopyBufferToImage(image, *staging_block.cpu, data.offset, offset, extent, row_length, image_height, subresource);
		return data.host;
	}

	void* CommandBuffer::UpdateImage(const Image& image, uint32_t row_length, uint32_t image_height)
	{
		const VkImageSubresourceLayers subresource = {
			FormatToAspectMask(image.GetFormat()), 0, 0, 1,
		};
		return UpdateImage(image, { 0, 0, 0 }, { image.GetWidth(), image.GetHeight(), image.GetDepth() }, row_length, image_height, subresource);
	}

	void CommandBuffer::SetUniformBuffer(unsigned set, unsigned binding, unsigned array_index, const Buffer& buffer, VkDeviceSize offset, VkDeviceSize range)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
		VK_ASSERT(buffer.GetCreateInfo().usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		VK_ASSERT(current_layout);
		VK_ASSERT(current_layout->HasDescriptorSet(set));
		VK_ASSERT(current_layout->HasDescriptorBinding(set, binding));
		VK_ASSERT(array_index < current_layout->GetArraySize(set, binding));

		auto& b = current_layout->GetDescriptor(set, binding, array_index);

		if (buffer.GetCookie() == b.cookie && b.resource.buffer.range == range)
		{
			if (b.resource.dynamic_offset != offset)
			{
				//If just the offset changed, indicate that the dynamic set is dirty
				dirty_sets_dynamic |= 1u << set;
				b.resource.dynamic_offset = offset;
			}
		}
		else
		{
			b.resource.buffer = { buffer.GetBuffer(), 0, range };
			b.resource.dynamic_offset = offset;
			b.cookie = buffer.GetCookie();
			b.secondary_cookie = 0;
			//Indicate that a static set is dirty
			dirty_sets |= 1u << set;
		}
	}

	void CommandBuffer::SetStorageBuffer(unsigned set, unsigned binding, unsigned array_index, const Buffer& buffer, VkDeviceSize offset, VkDeviceSize range)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
		VK_ASSERT(buffer.GetCreateInfo().usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		VK_ASSERT(current_layout);
		VK_ASSERT(current_layout->HasDescriptorSet(set));
		VK_ASSERT(current_layout->HasDescriptorBinding(set, binding));
		VK_ASSERT(array_index < current_layout->GetArraySize(set, binding));
		auto& b = current_layout->GetDescriptor(set, binding, array_index);

		if (buffer.GetCookie() == b.cookie && b.resource.buffer.offset == offset && b.resource.buffer.range == range)
			return;

		b.resource.buffer = { buffer.GetBuffer(), offset, range };
		b.resource.dynamic_offset = 0;
		b.cookie = buffer.GetCookie();
		b.secondary_cookie = 0;
		dirty_sets |= 1u << set;
	}

	void CommandBuffer::SetUniformBuffer(unsigned set, unsigned binding, unsigned array_index, const Buffer& buffer)
	{
		SetUniformBuffer(set, binding, array_index, buffer, 0, buffer.GetCreateInfo().size);
	}

	void CommandBuffer::SetStorageBuffer(unsigned set, unsigned binding, unsigned array_index, const Buffer& buffer)
	{
		SetStorageBuffer(set, binding, array_index, buffer, 0, buffer.GetCreateInfo().size);
	}

	void CommandBuffer::SetSampler(unsigned set, unsigned binding, unsigned array_index, const Sampler& sampler)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
		VK_ASSERT(current_layout);
		VK_ASSERT(current_layout->HasDescriptorSet(set));
		VK_ASSERT(current_layout->HasDescriptorBinding(set, binding));
		VK_ASSERT(array_index < current_layout->GetArraySize(set, binding));

		auto& b = current_layout->GetDescriptor(set, binding, array_index);

		if (sampler.GetCookie() == b.secondary_cookie)
			return;

		b.resource.image.fp.sampler = sampler.get_sampler();
		b.resource.image.integer.sampler = sampler.get_sampler();
		//Indicate that the set must be updated
		dirty_sets |= 1u << set;
		b.secondary_cookie = sampler.GetCookie();
	}

	void CommandBuffer::SetBufferView(unsigned set, unsigned binding, unsigned array_index, const BufferView& view)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
		VK_ASSERT(current_layout);
		VK_ASSERT(current_layout->HasDescriptorSet(set));
		VK_ASSERT(current_layout->HasDescriptorBinding(set, binding));
		VK_ASSERT(array_index < current_layout->GetArraySize(set, binding));
		VK_ASSERT(view.GetBuffer().GetCreateInfo().usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);

		auto& b = current_layout->GetDescriptor(set, binding, array_index);

		if (view.GetCookie() == b.cookie)
			return;

		b.resource.buffer_view = view.GetView();
		b.cookie = view.GetCookie();
		b.secondary_cookie = 0;
		dirty_sets |= 1u << set;
	}

	void CommandBuffer::SetInputAttachments(unsigned set, unsigned start_binding)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(start_binding + actual_render_pass->GetNumInputAttachments(pipeline_state.subpass_index) <= VULKAN_NUM_BINDINGS);
		unsigned num_input_attachments = actual_render_pass->GetNumInputAttachments(pipeline_state.subpass_index);
		for (unsigned i = 0; i < num_input_attachments; i++)
		{
			auto& ref = actual_render_pass->GetInputAttachment(pipeline_state.subpass_index, i);
			if (ref.attachment == VK_ATTACHMENT_UNUSED)
				continue;

			const ImageView* view = framebuffer_attachments[ref.attachment];
			VK_ASSERT(view);
			VK_ASSERT(view->GetImage().GetCreateInfo().usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);

			auto& b = current_layout->GetDescriptor(set, start_binding + i, 0);

			if (view->GetCookie() == b.cookie && b.resource.image.fp.imageLayout == ref.layout)
			{
				continue;
			}

			b.resource.image.fp.imageLayout = ref.layout;
			b.resource.image.integer.imageLayout = ref.layout;
			b.resource.image.fp.imageView = view->GetFloatView();
			b.resource.image.integer.imageView = view->GetIntegerView();

			b.cookie = view->GetCookie();
			dirty_sets |= 1u << set;
		}
	}

	void CommandBuffer::SetTexture(unsigned set, unsigned binding, unsigned array_index,
		VkImageView float_view, VkImageView integer_view,
		VkImageLayout layout,
		uint64_t cookie)
	{

		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
		VK_ASSERT(current_layout);
		VK_ASSERT(current_layout->HasDescriptorSet(set));
		VK_ASSERT(current_layout->HasDescriptorBinding(set, binding));
		VK_ASSERT(array_index < current_layout->GetArraySize(set, binding));

		auto& b = current_layout->GetDescriptor(set, binding, array_index);

		if (cookie == b.cookie && b.resource.image.fp.imageLayout == layout)
			return;

		b.resource.image.fp.imageLayout = layout;
		b.resource.image.fp.imageView = float_view;
		b.resource.image.integer.imageLayout = layout;
		b.resource.image.integer.imageView = integer_view;
		b.cookie = cookie;
		dirty_sets |= 1u << set;
	}

	/*void CommandBuffer::SetBindless(unsigned set, VkDescriptorSet desc_set)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		bindless_sets[set] = desc_set;
		dirty_sets |= 1u << set;
	}*/


	void CommandBuffer::SetSeparateTexture(unsigned set, unsigned binding, unsigned array_index, const ImageView& view)
	{
		VK_ASSERT(view.GetImage().GetCreateInfo().usage & VK_IMAGE_USAGE_SAMPLED_BIT);
		SetTexture(set, binding, array_index, view.GetFloatView(), view.GetIntegerView(), view.GetImage().GetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), view.GetCookie());
	}

	enum CookieBits
	{
		COOKIE_BIT_UNORM = 1 << 0,
		COOKIE_BIT_SRGB = 1 << 1
	};

	void CommandBuffer::SetSampledTexture(unsigned set, unsigned binding, unsigned array_index, const ImageView& view, const Sampler& sampler)
	{
		SetSampler(set, binding, array_index, sampler);
		SetSeparateTexture(set, binding, array_index, view);
	}

	void CommandBuffer::SetSampledTexture(unsigned set, unsigned binding, unsigned array_index, const ImageView& view, StockSampler stock)
	{
		VK_ASSERT(set < VULKAN_NUM_DESCRIPTOR_SETS);
		VK_ASSERT(binding < VULKAN_NUM_BINDINGS);
		VK_ASSERT(view.GetImage().GetCreateInfo().usage & VK_IMAGE_USAGE_SAMPLED_BIT);
		const auto& sampler = device->GetStockSampler(stock);
		SetSampledTexture(set, binding, array_index, view, sampler);
	}

	void CommandBuffer::SetSampler(unsigned set, unsigned binding, unsigned array_index, StockSampler stock)
	{
		const auto& sampler = device->GetStockSampler(stock);
		SetSampler(set, binding, array_index, sampler);
	}

	void CommandBuffer::SetStorageTexture(unsigned set, unsigned binding, unsigned array_index, const ImageView& view)
	{
		VK_ASSERT(view.GetImage().GetCreateInfo().usage & VK_IMAGE_USAGE_STORAGE_BIT);
		SetTexture(set, binding, array_index, view.GetFloatView(), view.GetIntegerView(), view.GetImage().GetLayout(VK_IMAGE_LAYOUT_GENERAL), view.GetCookie());
	}

	void CommandBuffer::RebindDescriptorSet(uint32_t set)
	{
		VK_ASSERT(current_layout);

		if (!current_layout->HasDescriptorSet(set))
			return;
		//auto& layout = current_layout->GetResourceLayout();
		////Bind any bindless descriptors
		//if (layout.bindless_descriptor_set_mask & (1u << set))
		//{
		//	VK_ASSERT(bindless_sets[set]);
		//	table.vkCmdBindDescriptorSets(cmd, actual_render_pass ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE, current_pipeline_layout, set, 1, &bindless_sets[set], 0, nullptr);
		//	return;
		//}

		auto& set_layout = current_layout->GetDescriptorSet(set)->set_layout;
		
		uint32_t num_dynamic_offsets = 0;
		// Allocate the max needed array size. This type of allocation is basically free, so this is fine
		Util::RetainedDynamicArray<uint32_t> dynamic_offsets = device->AllocateHeapArray<uint32_t>(current_layout->GetDescriptorCount(set));

		// UBOs
		Util::for_each_bit(set_layout.uniform_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				dynamic_offsets[num_dynamic_offsets++] = current_layout->GetDescriptor(set, binding, i).resource.dynamic_offset;
			}
			});

		table.vkCmdBindDescriptorSets(cmd, actual_render_pass ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE, current_pipeline_layout, set, 1, &allocated_sets[set], num_dynamic_offsets, dynamic_offsets.Data());

		device->FreeHeapArray(dynamic_offsets);
	}

	void CommandBuffer::FlushDescriptorSet(uint32_t set)
	{
		VK_ASSERT(current_layout);
		if (!current_layout->HasDescriptorSet(set))
			return;
		/*if (layout.bindless_descriptor_set_mask & (1u << set))
		{
			VK_ASSERT(bindless_sets[set]);
			table.vkCmdBindDescriptorSets(cmd, actual_render_pass ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE,
				current_pipeline_layout, set, 1, &bindless_sets[set], 0, nullptr);
			return;
		}*/

		auto& set_layout = current_layout->GetDescriptorSet(set)->set_layout;

		uint32_t num_dynamic_offsets = 0;
		// Allocate the max needed array size. This type of allocation is basically free, so this is fine
		Util::RetainedDynamicArray<uint32_t> dynamic_offsets = device->AllocateHeapArray<uint32_t>(current_layout->GetDescriptorCount(set));

		// Retrieve dynamic offsets
		Util::for_each_bit(set_layout.uniform_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				dynamic_offsets[num_dynamic_offsets++] = current_layout->GetDescriptor(set, binding, i).resource.dynamic_offset;
			}
			});

		// Gets the descriptor set (updates if the descriptor set has been changed)
		VkDescriptorSet desc_set = current_layout->FlushDescriptorSet(thread_index, set);

		table.vkCmdBindDescriptorSets(cmd, actual_render_pass ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE, current_pipeline_layout, set, 1, &desc_set, num_dynamic_offsets, dynamic_offsets.Data());

		device->FreeHeapArray(dynamic_offsets);

		allocated_sets[set] = desc_set;
	}

	void CommandBuffer::FlushDescriptorSets()
	{
		uint32_t set_update = current_layout->GetDescriptorSetMask() & dirty_sets;
		Util::for_each_bit(set_update, [&](uint32_t set) { FlushDescriptorSet(set); });
		dirty_sets &= ~set_update;

		// If we update a set, we also bind dynamically.
		dirty_sets_dynamic &= ~set_update;

		// If we only rebound UBOs, we might get away with just rebinding descriptor sets, no hashing and lookup required.
		uint32_t dynamic_set_update = current_layout->GetDescriptorSetMask() & dirty_sets_dynamic;
		Util::for_each_bit(dynamic_set_update, [&](uint32_t set) { RebindDescriptorSet(set); });
		dirty_sets_dynamic &= ~dynamic_set_update;

	}

	void CommandBuffer::Draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
	{
		VK_ASSERT(!is_compute);
		if (FlushRenderState(true))
		{
			table.vkCmdDraw(cmd, vertex_count, instance_count, first_vertex, first_instance);
		}
		else
			QM_LOG_ERROR("Failed to flush render state, draw call will be dropped.\n");
	}

	void CommandBuffer::DrawIndexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
	{
		VK_ASSERT(!is_compute);
		VK_ASSERT(index_state.buffer != VK_NULL_HANDLE);
		if (FlushRenderState(true))
		{
			table.vkCmdDrawIndexed(cmd, index_count, instance_count, first_index, vertex_offset, first_instance);
		}
		else
			QM_LOG_ERROR("Failed to flush render state, draw call will be dropped.\n");
	}

	void CommandBuffer::DrawIndirect(const Vulkan::Buffer& buffer, uint32_t offset, uint32_t draw_count, uint32_t stride)
	{
		VK_ASSERT(!is_compute);
		if (FlushRenderState(true))
		{
			table.vkCmdDrawIndirect(cmd, buffer.GetBuffer(), offset, draw_count, stride);
		}
		else
			QM_LOG_ERROR("Failed to flush render state, draw call will be dropped.\n");
	}

	void CommandBuffer::DrawIndexedIndirect(const Vulkan::Buffer& buffer,
		uint32_t offset, uint32_t draw_count, uint32_t stride)
	{
		VK_ASSERT(!is_compute);
		if (FlushRenderState(true))
		{
			table.vkCmdDrawIndexedIndirect(cmd, buffer.GetBuffer(), offset, draw_count, stride);
		}
		else
			QM_LOG_ERROR("Failed to flush render state, draw call will be dropped.\n");
	}

	void CommandBuffer::DrawMultiIndirect(const Buffer& buffer, uint32_t offset, uint32_t draw_count, uint32_t stride, const Buffer& count, uint32_t count_offset)
	{
		VK_ASSERT(!is_compute);
		if (!GetDevice().GetDeviceExtensions().supports_draw_indirect_count)
		{
			QM_LOG_ERROR("VK_KHR_draw_indirect_count not supported, dropping draw call.\n");
			return;
		}

		if (FlushRenderState(true))
		{
			table.vkCmdDrawIndirectCountKHR(cmd, buffer.GetBuffer(), offset,
				count.GetBuffer(), count_offset,
				draw_count, stride);
		}
		else
			QM_LOG_ERROR("Failed to flush render state, draw call will be dropped.\n");
	}

	void CommandBuffer::DrawIndexedMultiIndirect(const Buffer& buffer, uint32_t offset, uint32_t draw_count, uint32_t stride, const Buffer& count, uint32_t count_offset)
	{
		VK_ASSERT(!is_compute);
		if (!GetDevice().GetDeviceExtensions().supports_draw_indirect_count)
		{
			QM_LOG_ERROR("VK_KHR_draw_indirect_count not supported, dropping draw call.\n");
			return;
		}

		if (FlushRenderState(true))
		{
			table.vkCmdDrawIndexedIndirectCountKHR(cmd, buffer.GetBuffer(), offset, count.GetBuffer(), count_offset, draw_count, stride);
		}
		else
			QM_LOG_ERROR("Failed to flush render state, draw call will be dropped.\n");
	}

	void CommandBuffer::Dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z)
	{
		VK_ASSERT(is_compute);
		if (FlushComputeState(true))
		{
			table.vkCmdDispatch(cmd, groups_x, groups_y, groups_z);
		}
		else
			QM_LOG_ERROR("Failed to flush render state, dispatch will be dropped.\n");
	}

	void CommandBuffer::DispatchIndirect(const Buffer& buffer, uint32_t offset)
	{
		VK_ASSERT(is_compute);
		if (FlushComputeState(true))
		{
			table.vkCmdDispatchIndirect(cmd, buffer.GetBuffer(), offset);
		}
		else
			QM_LOG_ERROR("Failed to flush render state, dispatch will be dropped.\n");
	}

	void CommandBuffer::ClearRenderState()
	{
		// Preserve spec constant mask.
		auto& state = pipeline_state.static_state.state;
		memset(&state, 0, sizeof(state));
	}

	void CommandBuffer::SetOpaqueState()
	{
		ClearRenderState();
		auto& state = pipeline_state.static_state.state;
		state.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		state.cull_mode = VK_CULL_MODE_BACK_BIT;
		state.blend_enable = false;
		state.depth_test = true;
		state.depth_compare = VK_COMPARE_OP_LESS_OR_EQUAL;
		state.depth_write = true;
		state.depth_bias_enable = false;
		state.primitive_restart = false;
		state.stencil_test = false;
		state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		state.write_mask = ~0u;
		set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	}


	void CommandBuffer::SetQuadState()
	{
		ClearRenderState();
		auto& state = pipeline_state.static_state.state;
		state.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		state.cull_mode = VK_CULL_MODE_NONE;
		state.blend_enable = false;
		state.depth_test = false;
		state.depth_write = false;
		state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		state.write_mask = ~0u;
		set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	}

	void CommandBuffer::SetOpaqueSpriteState()
	{
		ClearRenderState();
		auto& state = pipeline_state.static_state.state;
		state.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		state.cull_mode = VK_CULL_MODE_NONE;
		state.blend_enable = false;
		state.depth_compare = VK_COMPARE_OP_LESS;
		state.depth_test = true;
		state.depth_write = true;
		state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		state.write_mask = ~0u;
		set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	}

	void CommandBuffer::SetTransparentSpriteState()
	{
		ClearRenderState();
		auto& state = pipeline_state.static_state.state;
		state.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		state.cull_mode = VK_CULL_MODE_NONE;
		state.blend_enable = true;
		state.depth_test = true;
		state.depth_compare = VK_COMPARE_OP_LESS;
		state.depth_write = false;
		state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		state.write_mask = ~0u;

		// The alpha layer should start at 1 (fully transparent).
		// As layers are blended in, the transparency is multiplied with other transparencies (1 - alpha).
		SetBlendFactors(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
		SetBlendOp(VK_BLEND_OP_ADD);

		set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
	}

	void CommandBuffer::SaveState(CommandBufferSaveStateFlags flags, CommandBufferSavedState& state)
	{
		if (flags & COMMAND_BUFFER_SAVED_VIEWPORT_BIT)
			state.viewport = viewport;
		if (flags & COMMAND_BUFFER_SAVED_SCISSOR_BIT)
			state.scissor = scissor;
		if (flags & COMMAND_BUFFER_SAVED_RENDER_STATE_BIT)
		{
			memcpy(&state.static_state, &pipeline_state.static_state, sizeof(pipeline_state.static_state));
			state.potential_static_state = pipeline_state.potential_static_state;
			state.dynamic_state = dynamic_state;
		}

		if (flags & COMMAND_BUFFER_SAVED_PUSH_CONSTANT_BIT)
			memcpy(state.push_constant_data, push_constant_data, VULKAN_PUSH_CONSTANT_SIZE);

		state.flags = flags;
	}

	void CommandBuffer::RestoreState(const CommandBufferSavedState& state)
	{
		auto& static_state = pipeline_state.static_state;
		auto& potential_static_state = pipeline_state.potential_static_state;

		if (state.flags & COMMAND_BUFFER_SAVED_PUSH_CONSTANT_BIT)
		{
			if (memcmp(state.push_constant_data, push_constant_data, VULKAN_PUSH_CONSTANT_SIZE) != 0)
			{
				memcpy(push_constant_data, state.push_constant_data, VULKAN_PUSH_CONSTANT_SIZE);
				set_dirty(COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT);
			}
		}

		if ((state.flags & COMMAND_BUFFER_SAVED_VIEWPORT_BIT) && memcmp(&state.viewport, &viewport, sizeof(viewport)) != 0)
		{
			viewport = state.viewport;
			set_dirty(COMMAND_BUFFER_DIRTY_VIEWPORT_BIT);
		}

		if ((state.flags & COMMAND_BUFFER_SAVED_SCISSOR_BIT) && memcmp(&state.scissor, &scissor, sizeof(scissor)) != 0)
		{
			scissor = state.scissor;
			set_dirty(COMMAND_BUFFER_DIRTY_SCISSOR_BIT);
		}

		if (state.flags & COMMAND_BUFFER_SAVED_RENDER_STATE_BIT)
		{
			if (memcmp(&state.static_state, &static_state, sizeof(static_state)) != 0)
			{
				memcpy(&static_state, &state.static_state, sizeof(static_state));
				set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
			}

			if (memcmp(&state.potential_static_state, &potential_static_state, sizeof(potential_static_state)) != 0)
			{
				memcpy(&potential_static_state, &state.potential_static_state, sizeof(potential_static_state));
				set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
			}

			if (memcmp(&state.dynamic_state, &dynamic_state, sizeof(dynamic_state)) != 0)
			{
				memcpy(&dynamic_state, &state.dynamic_state, sizeof(dynamic_state));
				set_dirty(COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT | COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT);
			}
		}
	}

	void CommandBuffer::End()
	{
		if (table.vkEndCommandBuffer(cmd) != VK_SUCCESS)
			QM_LOG_ERROR("Failed to end command buffer.\n");

		if (vbo_block.mapped)
			device->RequestVertexBlockNolock(vbo_block, 0);
		if (ibo_block.mapped)
			device->RequestIndexBlockNolock(ibo_block, 0);
		if (ubo_block.mapped)
			device->RequestUniformBlockNolock(ubo_block, 0);
		if (staging_block.mapped)
			device->RequestStagingBlockNolock(staging_block, 0);

		pipeline_state.program.Reset();
	}

	//////////////////////////////////
	//Command Buffer Deleter//////////
	//////////////////////////////////

	void CommandBufferDeleter::operator()(Vulkan::CommandBuffer* cmd)
	{
		cmd->device->handle_pool.command_buffers.free(cmd);
	}



}