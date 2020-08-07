#include "command_buffer.hpp"

#include "device.hpp"
#include "format.hpp"
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
		memset(&bindings, 0, sizeof(bindings));
	}

	CommandBuffer::~CommandBuffer()
	{
		VK_ASSERT(vbo_block.mapped == nullptr);
		VK_ASSERT(ibo_block.mapped == nullptr);
		VK_ASSERT(ubo_block.mapped == nullptr);
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
		VK_ASSERT(dst.get_create_info().size == src.get_create_info().size);
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
			region.srcSubresource.aspectMask = format_to_aspect_mask(src.GetFormat());
			region.srcSubresource.layerCount = src.GetCreateInfo().layers;
			region.dstSubresource.aspectMask = format_to_aspect_mask(dst.GetFormat());
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

	void CommandBuffer::CopyBufferToImage(const Image& image, const Buffer& src, VkDeviceSize buffer_offset,
		const VkOffset3D& offset, const VkExtent3D& extent, unsigned row_length,
		unsigned slice_height, const VkImageSubresourceLayers& subresource)
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
		auto aspect = format_to_aspect_mask(image.GetFormat());
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
		barrier.subresourceRange.aspectMask = format_to_aspect_mask(image.GetCreateInfo().format);
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
		auto event = device->request_pipeline_event();
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

		blit.srcSubresource.aspectMask = format_to_aspect_mask(src.GetCreateInfo().format);
		blit.srcSubresource.mipLevel = src_level;
		blit.srcSubresource.baseArrayLayer = src_base_layer;
		blit.srcSubresource.layerCount = num_layers;
		blit.srcOffsets[0] = src_offset;
		blit.srcOffsets[1] = add_offset(src_offset, src_extent);

		blit.dstSubresource.aspectMask = format_to_aspect_mask(dst.GetCreateInfo().format);
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

			blit.srcSubresource.aspectMask = format_to_aspect_mask(src.GetCreateInfo().format);
			blit.srcSubresource.mipLevel = src_level;
			blit.srcSubresource.baseArrayLayer = src_base_layer + i;
			blit.srcSubresource.layerCount = 1;
			blit.srcOffsets[0] = src_offset;
			blit.srcOffsets[1] = add_offset(src_offset, src_extent);

			blit.dstSubresource.aspectMask = format_to_aspect_mask(dst.GetCreateInfo().format);
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
			barriers[i].subresourceRange.aspectMask = format_to_aspect_mask(image.GetFormat());
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
		b.subresourceRange.aspectMask = format_to_aspect_mask(image.GetFormat());
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
		pipeline_state.program = nullptr;
		memset(bindings.cookies, 0, sizeof(bindings.cookies));
		memset(bindings.secondary_cookies, 0, sizeof(bindings.secondary_cookies));
		memset(&index_state, 0, sizeof(index_state));
		memset(vbo.buffers, 0, sizeof(vbo.buffers));

		if (debug_channel_buffer)
			SetStorageBuffer(VULKAN_NUM_DESCRIPTOR_SETS - 1, VULKAN_NUM_BINDINGS - 1, *debug_channel_buffer);
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
		rect.offset.x = std::min(fb->get_width(), uint32_t(rect.offset.x));
		rect.offset.y = std::min(fb->get_height(), uint32_t(rect.offset.y));
		rect.extent.width = std::min(fb->get_width() - rect.offset.x, rect.extent.width);
		rect.extent.height = std::min(fb->get_height() - rect.offset.y, rect.extent.height);

		viewport = { 0.0f, 0.0f, float(fb->get_width()), float(fb->get_height()), 0.0f, 1.0f };
		scissor = rect;
	}

	CommandBufferHandle CommandBuffer::RequestSecondaryCommandBuffer(Device& device, const RenderPassInfo& info, unsigned thread_index, unsigned subpass)
	{
		auto* fb = &device.request_framebuffer(info);
		auto cmd = device.request_secondary_command_buffer_for_thread(thread_index, fb, subpass);
		cmd->BeginGraphics();

		cmd->framebuffer = fb;
		cmd->pipeline_state.compatible_render_pass = &fb->get_compatible_render_pass();
		cmd->actual_render_pass = &device.request_render_pass(info, false);

		unsigned i;
		for (i = 0; i < info.num_color_attachments; i++)
			cmd->framebuffer_attachments[i] = info.color_attachments[i];
		if (info.depth_stencil)
			cmd->framebuffer_attachments[i++] = info.depth_stencil;

		cmd->InitViewportScissor(info, fb);
		cmd->pipeline_state.subpass_index = subpass;
		cmd->current_contents = VK_SUBPASS_CONTENTS_INLINE;

		return cmd;
	}

	CommandBufferHandle CommandBuffer::RequestSecondaryCommandBuffer(unsigned thread_index_, unsigned subpass_)
	{
		VK_ASSERT(framebuffer);
		VK_ASSERT(!is_secondary);

		auto secondary_cmd = device->request_secondary_command_buffer_for_thread(thread_index_, framebuffer, subpass_);
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

		device->submit_secondary(*this, *secondary);
	}

	void CommandBuffer::NextSubpass(VkSubpassContents contents)
	{
		VK_ASSERT(framebuffer);
		VK_ASSERT(pipeline_state.compatible_render_pass);
		VK_ASSERT(actual_render_pass);
		pipeline_state.subpass_index++;
		VK_ASSERT(pipeline_state.subpass_index < actual_render_pass->get_num_subpasses());
		table.vkCmdNextSubpass(cmd, contents);
		current_contents = contents;
		BeginGraphics();
	}

	void CommandBuffer::BeginRenderPass(const RenderPassInfo& info, VkSubpassContents contents)
	{
		VK_ASSERT(!framebuffer);
		VK_ASSERT(!pipeline_state.compatible_render_pass);
		VK_ASSERT(!actual_render_pass);

		framebuffer = &device->request_framebuffer(info);
		pipeline_state.compatible_render_pass = &framebuffer->get_compatible_render_pass();
		actual_render_pass = &device->request_render_pass(info, false);
		pipeline_state.subpass_index = 0;

		memset(framebuffer_attachments, 0, sizeof(framebuffer_attachments));
		unsigned att;
		for (att = 0; att < info.num_color_attachments; att++)
			framebuffer_attachments[att] = info.color_attachments[att];
		if (info.depth_stencil)
			framebuffer_attachments[att++] = info.depth_stencil;

		InitViewportScissor(info, framebuffer);

		VkClearValue clear_values[VULKAN_NUM_ATTACHMENTS + 1];
		unsigned num_clear_values = 0;

		for (unsigned i = 0; i < info.num_color_attachments; i++)
		{
			VK_ASSERT(info.color_attachments[i]);
			if (info.clear_attachments & (1u << i))
			{
				clear_values[i].color = info.clear_color[i];
				num_clear_values = i + 1;
			}

			if (info.color_attachments[i]->GetImage().IsSwapchainImage())
				uses_swapchain = true;
		}

		if (info.depth_stencil && (info.op_flags & RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT) != 0)
		{
			clear_values[info.num_color_attachments].depthStencil = info.clear_depth_stencil;
			num_clear_values = info.num_color_attachments + 1;
		}

		VkRenderPassBeginInfo begin_info = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		VkRenderPassAttachmentBeginInfoKHR attachment_info = { VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO_KHR };
		begin_info.renderPass = actual_render_pass->get_render_pass();
		begin_info.framebuffer = framebuffer->get_framebuffer();
		begin_info.renderArea = scissor;
		begin_info.clearValueCount = num_clear_values;
		begin_info.pClearValues = clear_values;

		auto& features = device->GetDeviceFeatures();
		bool imageless = features.imageless_features.imagelessFramebuffer == VK_TRUE;
		VkImageView immediate_views[VULKAN_NUM_ATTACHMENTS + 1];
		if (imageless)
		{
			attachment_info.attachmentCount = Framebuffer::setup_raw_views(immediate_views, info);
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

}