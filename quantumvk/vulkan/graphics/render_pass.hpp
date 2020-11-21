#pragma once

#include "quantumvk/utils/hash.hpp"
#include "quantumvk/utils/intrusive.hpp"
#include "quantumvk/utils/object_pool.hpp"
#include "quantumvk/utils/temporary_hashmap.hpp"

#include "quantumvk/vulkan/images/image.hpp"

#include "quantumvk/vulkan/misc/cookie.hpp"
#include "quantumvk/vulkan/misc/limits.hpp"

#include "quantumvk/vulkan/vulkan_headers.hpp"

namespace Vulkan
{
	enum RenderPassOp
	{
		RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT = 1 << 0,
		RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT = 1 << 1,
		RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT = 1 << 2,
		RENDER_PASS_OP_ENABLE_TRANSIENT_STORE_BIT = 1 << 3,
		RENDER_PASS_OP_ENABLE_TRANSIENT_LOAD_BIT = 1 << 4
	};
	using RenderPassOpFlags = uint32_t;

	class ImageView;

	struct RenderPassInfo
	{
		struct ColorAttachment
		{
			// Pointer to ImageView of attachment
			ImageView* view = nullptr;
			// Layout that the attachment will be in at the start of the renderpass. VK_IMAGE_LAYOUT_UNDEFINED meanslayout doesn't matter, and the contents of the image can be destructively transitioned away from.
			// This is ignored if the view is a swapchain image. Initial_layout must not be UNDEFINED if this attachment is set to be loaded at the start of the pass.
			VkImageLayout initial_layout;
			// Layout that the attachment will be transitioned to at end of renderpass. VK_IMAGE_LAYOUT_UNDEFINED means it will use the layout from the last subpass.
			// This is ignored if the view is a swapchain image
			VkImageLayout final_layout;
			// Color the attachment will be cleared to at start of renderpass if this attachment's index bit is set in uint32_t clear_attachment.
			VkClearColorValue clear_color;
		};

		struct DepthStencilAttachment
		{
			// Pointer to ImageView of attachment
			ImageView* view = nullptr;
			// Layout that the attachment will be in at the start of the renderpass. VK_IMAGE_LAYOUT_UNDEFINED means it doesn't matter, and the contents of the image can be destructively transitioned away from.
			// Initial_layout must not be UNDEFINED if this attachment is set to be loaded at the start of the pass.
			VkImageLayout initial_layout;
			// Layout that the attachment will be transitioned to at end of renderpass. VK_IMAGE_LAYOUT_UNDEFINED means it will use the layout from the last subpass.
			VkImageLayout final_layout;
			// DepthStencilClearValue the attachment will be cleared to at start of renderpass if the RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT bit is set in op_flags
			VkClearDepthStencilValue clear_value = { 1.0f, 0 };
		};

		uint32_t num_color_attachments = 0;
		ColorAttachment color_attachments[VULKAN_NUM_ATTACHMENTS];

		uint32_t clear_attachments = 0;
		uint32_t load_attachments = 0;
		uint32_t store_attachments = 0;

		DepthStencilAttachment depth_stencil;
		RenderPassOpFlags op_flags = 0;

		uint32_t multiview_mask = 0;

		// Render area will be clipped to the actual framebuffer.
		VkRect2D render_area = { { 0, 0 }, { UINT32_MAX, UINT32_MAX } };

		enum class DepthStencil
		{
			None,
			ReadOnly,
			ReadWrite
		};

		struct Subpass
		{
			uint32_t num_color_attachments = 0;
			uint32_t color_attachments[VULKAN_NUM_ATTACHMENTS];
			uint32_t num_input_attachments = 0;
			uint32_t input_attachments[VULKAN_NUM_ATTACHMENTS];
			uint32_t num_resolve_attachments = 0;
			uint32_t resolve_attachments[VULKAN_NUM_ATTACHMENTS];
			DepthStencil depth_stencil_mode = DepthStencil::ReadWrite;
		};

		// If 0/nullptr, assume a default subpass.
		uint32_t num_subpasses = 0;
		const Subpass* subpasses = nullptr;

	};

	class RenderPass : public HashedObject<RenderPass>, public NoCopyNoMove
	{
	public:
		struct SubpassInfo
		{
			VkAttachmentReference color_attachments[VULKAN_NUM_ATTACHMENTS];
			unsigned num_color_attachments;
			VkAttachmentReference input_attachments[VULKAN_NUM_ATTACHMENTS];
			unsigned num_input_attachments;
			VkAttachmentReference depth_stencil_attachment;

			unsigned samples;
		};

		RenderPass(Util::Hash hash, Device* device, const RenderPassInfo& info);
		RenderPass(Util::Hash hash, Device* device, const VkRenderPassCreateInfo& create_info);
		~RenderPass();

		unsigned GetNumSubpasses() const
		{
			return unsigned(subpasses_info.size());
		}

		VkRenderPass GetRenderPass() const
		{
			return render_pass;
		}

		uint32_t GetSampleCount(unsigned subpass) const
		{
			VK_ASSERT(subpass < subpasses_info.size());
			return subpasses_info[subpass].samples;
		}

		unsigned GetNumColorAttachments(unsigned subpass) const
		{
			VK_ASSERT(subpass < subpasses_info.size());
			return subpasses_info[subpass].num_color_attachments;
		}

		unsigned GetNumInputAttachments(unsigned subpass) const
		{
			VK_ASSERT(subpass < subpasses_info.size());
			return subpasses_info[subpass].num_input_attachments;
		}

		const VkAttachmentReference& GetColorAttachment(unsigned subpass, unsigned index) const
		{
			VK_ASSERT(subpass < subpasses_info.size());
			VK_ASSERT(index < subpasses_info[subpass].num_color_attachments);
			return subpasses_info[subpass].color_attachments[index];
		}

		const VkAttachmentReference& GetInputAttachment(unsigned subpass, unsigned index) const
		{
			VK_ASSERT(subpass < subpasses_info.size());
			VK_ASSERT(index < subpasses_info[subpass].num_input_attachments);
			return subpasses_info[subpass].input_attachments[index];
		}

		bool HasDepth(unsigned subpass) const
		{
			VK_ASSERT(subpass < subpasses_info.size());
			return subpasses_info[subpass].depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED &&
				FormatHasDepthAspect(depth_stencil);
		}

		bool HasStencil(unsigned subpass) const
		{
			VK_ASSERT(subpass < subpasses_info.size());
			return subpasses_info[subpass].depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED &&
				FormatHasStencilAspect(depth_stencil);
		}

	private:
		Device* device;
		VkRenderPass render_pass = VK_NULL_HANDLE;

		VkFormat color_attachments[VULKAN_NUM_ATTACHMENTS] = {};
		VkFormat depth_stencil = VK_FORMAT_UNDEFINED;
		std::vector<SubpassInfo> subpasses_info;

		void SetupSubpasses(const VkRenderPassCreateInfo& create_info);

		void FixupRenderPassWorkaround(VkRenderPassCreateInfo& create_info, VkAttachmentDescription* attachments);
		void FixupWsiBarrier(VkRenderPassCreateInfo& create_info, VkAttachmentDescription* attachments);
	};

	class Framebuffer : public Cookie, public NoCopyNoMove, public InternalSyncEnabled
	{
	public:
		Framebuffer(Device* device, const RenderPass& rp, const RenderPassInfo& info);
		~Framebuffer();

		VkFramebuffer GetFramebuffer() const
		{
			return framebuffer;
		}

		static unsigned SetupRawViews(VkImageView* views, const RenderPassInfo& info);
		static void ComputeDimensions(const RenderPassInfo& info, uint32_t& width, uint32_t& height);
		static void ComputeAttachmentDimensions(const RenderPassInfo& info, unsigned index, uint32_t& width, uint32_t& height);

		uint32_t GetWidth() const
		{
			return width;
		}

		uint32_t GetHeight() const
		{
			return height;
		}

		const RenderPass& GetCompatibleRenderPass() const
		{
			return render_pass;
		}

	private:
		Device* device;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		const RenderPass& render_pass;
		RenderPassInfo info;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	static const unsigned VULKAN_FRAMEBUFFER_RING_SIZE = 8;
	class FramebufferAllocator
	{
	public:
		explicit FramebufferAllocator(Device* device);
		Framebuffer& RequestFramebuffer(const RenderPassInfo& info);

		void BeginFrame();
		void Clear();

	private:
		struct FramebufferNode : Util::TemporaryHashmapEnabled<FramebufferNode>,
			Util::IntrusiveListEnabled<FramebufferNode>,
			Framebuffer
		{
			FramebufferNode(Device* device_, const RenderPass& rp, const RenderPassInfo& info_)
				: Framebuffer(device_, rp, info_)
			{
				SetInternalSyncObject();
			}
		};

		Device* device;
		Util::TemporaryHashmap<FramebufferNode, VULKAN_FRAMEBUFFER_RING_SIZE, false> framebuffers;
#ifdef QM_VULKAN_MT
		std::mutex lock;
#endif
	};

	class AttachmentAllocator
	{
	public:
		AttachmentAllocator(Device* device_, bool transient_)
			: device(device_), transient(transient_)
		{
		}

		ImageView& RequestAttachment(uint32_t width, uint32_t height, VkFormat format, uint32_t index = 0, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, uint32_t layers = 1);

		void BeginFrame();
		void Clear();

	private:
		struct TransientNode : Util::TemporaryHashmapEnabled<TransientNode>, Util::IntrusiveListEnabled<TransientNode>
		{
			explicit TransientNode(ImageHandle image_, ImageViewHandle view_)
				: image(std::move(image_)), view(std::move(view_))
			{
			}

			ImageHandle image;
			ImageViewHandle view;
		};

		Device* device;
		Util::TemporaryHashmap<TransientNode, VULKAN_FRAMEBUFFER_RING_SIZE, false> attachments;
#ifdef QM_VULKAN_MT
		std::mutex lock;
#endif
		bool transient;
	};

	class TransientAttachmentAllocator : public AttachmentAllocator
	{
	public:
		explicit TransientAttachmentAllocator(Device* device_)
			: AttachmentAllocator(device_, true)
		{
		}
	};

	class PhysicalAttachmentAllocator : public AttachmentAllocator
	{
	public:
		explicit PhysicalAttachmentAllocator(Device* device_)
			: AttachmentAllocator(device_, false)
		{
		}
	};

}

