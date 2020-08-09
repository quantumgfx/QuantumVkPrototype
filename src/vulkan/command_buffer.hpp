#pragma once

#include "vulkan_headers.hpp"

#include "vulkan_common.hpp"
#include <string.h>

#include "memory/buffer.hpp"
#include "memory/buffer_pool.hpp"

#include "images/image.hpp"
#include "images/sampler.hpp"

#include "sync/pipeline_event.hpp"

#include "graphics/render_pass.hpp"
#include "graphics/shader.hpp"

#include "misc/limits.hpp"

namespace Vulkan
{
	class DebugChannelInterface;

	//Contains a list of wait has been changed, or what parts of the pipeline state are "dirty"
	enum CommandBufferDirtyBits
	{
		COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT = 1 << 0,
		COMMAND_BUFFER_DIRTY_PIPELINE_BIT = 1 << 1,

		COMMAND_BUFFER_DIRTY_VIEWPORT_BIT = 1 << 2,
		COMMAND_BUFFER_DIRTY_SCISSOR_BIT = 1 << 3,
		COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT = 1 << 4,
		COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT = 1 << 5,

		COMMAND_BUFFER_DIRTY_STATIC_VERTEX_BIT = 1 << 6,

		COMMAND_BUFFER_DIRTY_PUSH_CONSTANTS_BIT = 1 << 7,

		COMMAND_BUFFER_DYNAMIC_BITS = COMMAND_BUFFER_DIRTY_VIEWPORT_BIT | COMMAND_BUFFER_DIRTY_SCISSOR_BIT |
		COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT |
		COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT
	};
	using CommandBufferDirtyFlags = uint32_t;

#define COMPARE_OP_BITS 3
#define STENCIL_OP_BITS 3
#define BLEND_FACTOR_BITS 5
#define BLEND_OP_BITS 3
#define CULL_MODE_BITS 2
#define FRONT_FACE_BITS 1

	union PipelineState {
		struct
		{
			// Depth state.
			unsigned depth_write : 1;
			unsigned depth_test : 1;
			unsigned blend_enable : 1;

			unsigned cull_mode : CULL_MODE_BITS;
			unsigned front_face : FRONT_FACE_BITS;
			unsigned depth_bias_enable : 1;

			unsigned depth_compare : COMPARE_OP_BITS;

			unsigned stencil_test : 1;
			unsigned stencil_front_fail : STENCIL_OP_BITS;
			unsigned stencil_front_pass : STENCIL_OP_BITS;
			unsigned stencil_front_depth_fail : STENCIL_OP_BITS;
			unsigned stencil_front_compare_op : COMPARE_OP_BITS;
			unsigned stencil_back_fail : STENCIL_OP_BITS;
			unsigned stencil_back_pass : STENCIL_OP_BITS;
			unsigned stencil_back_depth_fail : STENCIL_OP_BITS;
			unsigned stencil_back_compare_op : COMPARE_OP_BITS;

			unsigned alpha_to_coverage : 1;
			unsigned alpha_to_one : 1;
			unsigned sample_shading : 1;

			unsigned src_color_blend : BLEND_FACTOR_BITS;
			unsigned dst_color_blend : BLEND_FACTOR_BITS;
			unsigned color_blend_op : BLEND_OP_BITS;
			unsigned src_alpha_blend : BLEND_FACTOR_BITS;
			unsigned dst_alpha_blend : BLEND_FACTOR_BITS;
			unsigned alpha_blend_op : BLEND_OP_BITS;
			unsigned primitive_restart : 1;
			unsigned topology : 4;

			unsigned wireframe : 1;
			unsigned subgroup_control_size : 1;
			unsigned subgroup_full_group : 1;
			unsigned subgroup_minimum_size_log2 : 3;
			unsigned subgroup_maximum_size_log2 : 3;
			unsigned conservative_raster : 1;

			uint32_t write_mask;
		} state;
		uint32_t words[4];
	};

	struct PotentialState
	{
		float blend_constants[4];
		uint32_t spec_constants[VULKAN_NUM_SPEC_CONSTANTS];
		uint8_t spec_constant_mask;
	};

	struct DynamicState
	{
		float depth_bias_constant = 0.0f;
		float depth_bias_slope = 0.0f;
		uint8_t front_compare_mask = 0;
		uint8_t front_write_mask = 0;
		uint8_t front_reference = 0;
		uint8_t back_compare_mask = 0;
		uint8_t back_write_mask = 0;
		uint8_t back_reference = 0;
	};

	struct VertexAttribState
	{
		uint32_t binding;
		VkFormat format;
		uint32_t offset;
	};

	struct IndexState
	{
		VkBuffer buffer;
		VkDeviceSize offset;
		VkIndexType index_type;
	};

	struct VertexBindingState
	{
		VkBuffer buffers[VULKAN_NUM_VERTEX_BUFFERS];
		VkDeviceSize offsets[VULKAN_NUM_VERTEX_BUFFERS];
	};

	enum CommandBufferSavedStateBits
	{
		COMMAND_BUFFER_SAVED_BINDINGS_0_BIT = 1u << 0,
		COMMAND_BUFFER_SAVED_BINDINGS_1_BIT = 1u << 1,
		COMMAND_BUFFER_SAVED_BINDINGS_2_BIT = 1u << 2,
		COMMAND_BUFFER_SAVED_BINDINGS_3_BIT = 1u << 3,
		COMMAND_BUFFER_SAVED_BINDINGS_4_BIT = 1u << 4,
		COMMAND_BUFFER_SAVED_BINDINGS_5_BIT = 1u << 5,
		COMMAND_BUFFER_SAVED_BINDINGS_6_BIT = 1u << 6,
		COMMAND_BUFFER_SAVED_BINDINGS_7_BIT = 1u << 7,
		COMMAND_BUFFER_SAVED_VIEWPORT_BIT = 1u << 8,
		COMMAND_BUFFER_SAVED_SCISSOR_BIT = 1u << 9,
		COMMAND_BUFFER_SAVED_RENDER_STATE_BIT = 1u << 10,
		COMMAND_BUFFER_SAVED_PUSH_CONSTANT_BIT = 1u << 11
	};
	static_assert(VULKAN_NUM_DESCRIPTOR_SETS == 8, "Number of descriptor sets != 8.");
	using CommandBufferSaveStateFlags = uint32_t;

	struct CommandBufferSavedState
	{
		CommandBufferSaveStateFlags flags = 0;
		ResourceBindings bindings;
		VkViewport viewport;
		VkRect2D scissor;

		PipelineState static_state;
		PotentialState potential_static_state;
		DynamicState dynamic_state;
	};

	struct DeferredPipelineCompile
	{
		Program* program;
		const RenderPass* compatible_render_pass;
		PipelineState static_state;
		PotentialState potential_static_state;
		VertexAttribState attribs[VULKAN_NUM_VERTEX_ATTRIBS];
		VkDeviceSize strides[VULKAN_NUM_VERTEX_BUFFERS];
		VkVertexInputRate input_rates[VULKAN_NUM_VERTEX_BUFFERS];

		unsigned subpass_index;
		Util::Hash hash;
		VkPipelineCache cache;
	};

	class CommandBuffer;
	//Functor to delete command buffer
	struct CommandBufferDeleter
	{
		void operator()(CommandBuffer* cmd);
	};

	//Forward declare device
	class Device;

	class CommandBuffer : public Util::IntrusivePtrEnabled<CommandBuffer, CommandBufferDeleter, HandleCounter>
	{
	public:
		friend struct CommandBufferDeleter;
		//Type of Command buffer
		enum class Type
		{
			Generic,
			AsyncGraphics,
			AsyncCompute,
			AsyncTransfer,
			Count
		};

		//Destructs the Command Buffer
		~CommandBuffer();
		//Get Vulkan Command Buffer handle
		VkCommandBuffer GetCommandBuffer() const
		{
			return cmd;
		}

		Device& GetDevice()
		{
			return *device;
		}

		//Returns whether the command buffer uses a renderpass that involves the swapchain
		bool SwapchainTouched() const
		{
			return uses_swapchain;
		}

		void SetThreadIndex(unsigned index_)
		{
			thread_index = index_;
		}

		unsigned GetThreadIndex() const
		{
			return thread_index;
		}

		void SetIsSecondary()
		{
			is_secondary = true;
		}

		bool GetIsSecondary() const
		{
			return is_secondary;
		}

		//Fill a buffer with a specific value.
		//Executes in: VK_PIPELINE_STAGE_TRANSFER_BIT.
		//Buffer must have usage VK_BUFFER_USAGE_TRANSFER_DST_BIT.
		//Equivelent vulkan function: vkCmdFillBuffer()
		void FillBuffer(const Buffer& dst, uint32_t value);
		//Fill a buffer with a specific value. Starting at
		//offset and up to offset + size.
		//Executes in: VK_PIPELINE_STAGE_TRANSFER_BIT.
		//Buffer must have usage VK_BUFFER_USAGE_TRANSFER_DST_BIT.
		//Equivelent vulkan function: vkCmdFillBuffer()
		void FillBuffer(const Buffer& dst, uint32_t value, VkDeviceSize offset, VkDeviceSize size);
		//Copy one Buffer to another.
		//Executes in: VK_PIPELINE_STAGE_TRANSFER_BIT.
		//Src buffer must have usage VK_BUFFER_USAGE_TRANSFER_SRC_BIT.
		//Dst Buffer must have usage VK_BUFFER_USAGE_TRANSFER_DST_BIT.
		//Equivelent vulkan function: vkCmdCopyBuffer()
		void CopyBuffer(const Buffer& dst, VkDeviceSize dst_offset, const Buffer& src, VkDeviceSize src_offset, VkDeviceSize size);
		//Copy one Buffer to another.
		//Executes in: VK_PIPELINE_STAGE_TRANSFER_BIT.
		//Src buffer must have usage VK_BUFFER_USAGE_TRANSFER_SRC_BIT.
		//Dst Buffer must have usage VK_BUFFER_USAGE_TRANSFER_DST_BIT.
		//Equivelent vulkan function: vkCmdCopyBuffer()
		void CopyBuffer(const Buffer& dst, const Buffer& src);
		//Copy one Buffer to another.
		//Executes in: VK_PIPELINE_STAGE_TRANSFER_BIT.
		//Src buffer must have usage VK_BUFFER_USAGE_TRANSFER_SRC_BIT.
		//Dst Buffer must have usage VK_BUFFER_USAGE_TRANSFER_DST_BIT.
		//Equivelent vulkan function: vkCmdCopyBuffer()
		void CopyBuffer(const Buffer& dst, const Buffer& src, const VkBufferCopy* copies, size_t count);
		//Copy one Image to another
		//Executes in: VK_PIPELINE_STAGE_TRANSFER_BIT.
		//Src Image must be in layout VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL_BIT
		//Dst Image must be in layout VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL_BIT
		//Equivelent vulkan function: vkCmdCopyImage()
		void CopyImage(const Image& dst, const Image& src);
		//Copy one Image to another
		//Executes in: VK_PIPELINE_STAGE_TRANSFER_BIT.
		//Src Image must be in layout VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL_BIT
		//Dst Image must be in layout VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL_BIT
		//Equivelent vulkan function: vkCmdCopyImage()
		void CopyImage(const Image& dst, const Image& src,
			const VkOffset3D& dst_offset, const VkOffset3D& src_offset,
			const VkExtent3D& extent,
			const VkImageSubresourceLayers& dst_subresource,
			const VkImageSubresourceLayers& src_subresource);
		//Copy a buffer to an image
		//Executes in: VK_PIPELINE_STAGE_TRANSFER_BIT.
		//Src Buffer must have usage VK_BUFFER_USAGE_TRANSFER_SRC_BIT.
		//Dst Image must be in layout VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL_BIT
		//Equivelent vulkan function: vkCmdCopyBufferToImage()
		void CopyBufferToImage(const Image& image, const Buffer& buffer, VkDeviceSize buffer_offset,
			const VkOffset3D& offset, const VkExtent3D& extent, unsigned row_length,
			unsigned slice_height, const VkImageSubresourceLayers& subresrouce);
		//Copy a buffer to an image
		//Executes in: VK_PIPELINE_STAGE_TRANSFER_BIT.
		//Src Buffer must have usage VK_BUFFER_USAGE_TRANSFER_SRC_BIT.
		//Dst Image must be in layout VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL_BIT
		//Equivelent vulkan function: vkCmdCopyBufferToImage()
		void CopyBufferToImage(const Image& image, const Buffer& buffer, unsigned num_blits, const VkBufferImageCopy* blits);
		//Copy an image to a buffer
		//Executes in: VK_PIPELINE_STAGE_TRANSFER_BIT.
		//Src Image must be in layout VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL_BIT
		//Dst Buffer must have usage VK_BUFFER_USAGE_TRANSFER_DST_BIT.
		//Equivelent vulkan function: vkCmdCopyImageToBuffer()
		void CopyImageToBuffer(const Buffer& buffer, const Image& image, unsigned num_blits, const VkBufferImageCopy* blits);
		//Copy an image to a buffer
		//Executes in: VK_PIPELINE_STAGE_TRANSFER_BIT.
		//Src Image must be in layout VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL_BIT
		//Dst Buffer must have usage VK_BUFFER_USAGE_TRANSFER_DST_BIT.
		//Equivelent vulkan function: vkCmdCopyImageToBuffer()
		void CopyImageToBuffer(const Buffer& buffer, const Image& image, VkDeviceSize buffer_offset, const VkOffset3D& offset,
			const VkExtent3D& extent, unsigned row_length, unsigned slice_height,
			const VkImageSubresourceLayers& subresrouce);

		void ClearImage(const Image& image, const VkClearValue& value);
		void ClearImage(const Image& image, const VkClearValue& value, VkImageAspectFlags aspect);
		void ClearQuad(unsigned attachment, const VkClearRect& rect, const VkClearValue& value, VkImageAspectFlags = VK_IMAGE_ASPECT_COLOR_BIT);
		void ClearQuad(const VkClearRect& rect, const VkClearAttachment* attachments, unsigned num_attachments);

		//Ensures that all commands before this one will be finished before any commands after the barrier.
		void FullBarrier();
		//Ensures that all current draws to color attachments will be finished before input reads after this barrier. (Executes with dependency VK_DEPENDENCY_BY_REGION_BIT).
		void PixelBarrier();
		//Inserts a pipeline memory barrier.
		void Barrier(VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);
		//Inserts a pipeline barrier
		void Barrier(VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
			unsigned barriers, const VkMemoryBarrier* globals,
			unsigned buffer_barriers, const VkBufferMemoryBarrier* buffers,
			unsigned image_barriers, const VkImageMemoryBarrier* images);
		//Inserts a pipeline buffer barrier
		void BufferBarrier(const Buffer& buffer, VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access);
		//Inserts a pipeline image barrier
		void ImageBarrier(const Image& image, VkImageLayout old_layout, VkImageLayout new_layout,
			VkPipelineStageFlags src_stage, VkAccessFlags src_access, VkPipelineStageFlags dst_stage,
			VkAccessFlags dst_access);

		PipelineEvent SignalEvent(VkPipelineStageFlags stages);
		void WaitEvents(unsigned num_events, const VkEvent* events,
			VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages,
			unsigned barriers, const VkMemoryBarrier* globals,
			unsigned buffer_barriers, const VkBufferMemoryBarrier* buffers,
			unsigned image_barriers, const VkImageMemoryBarrier* images);

		//Blits one image into another
		void BlitImage(const Image& dst, const Image& src,
			const VkOffset3D& dst_offset, const VkOffset3D& dst_extent,
			const VkOffset3D& src_offset, const VkOffset3D& src_extent, unsigned dst_level, unsigned src_level,
			unsigned dst_base_layer = 0, uint32_t src_base_layer = 0, unsigned num_layers = 1,
			VkFilter filter = VK_FILTER_LINEAR);

		// Prepares an image to have its mipmap generated.
		// Puts the top-level into TRANSFER_SRC_OPTIMAL, and all other levels are invalidated with an UNDEFINED -> TRANSFER_DST_OPTIMAL.
		void BarrierPrepareGenerateMipmap(const Image& image, VkImageLayout base_level_layout, VkPipelineStageFlags src_stage, VkAccessFlags src_access, bool need_top_level_barrier = true);
		// The image must have been transitioned with BarrierPrepareGenerateMipmap before calling this function.
		// After calling this function, the image will be entirely in TRANSFER_SRC_OPTIMAL layout.
		// Wait for TRANSFER stage to drain before transitioning away from TRANSFER_SRC_OPTIMAL.
		void GenerateMipmap(const Image& image);
		//Begins a new renderpass, described by renderpassinfo. Retrieves the framebuffer and creates any uncreated attachment
		void BeginRenderPass(const RenderPassInfo& info, VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE);
		//Start Next subpass
		void NextSubpass(VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE);
		//End a renderpass
		void EndRenderPass();
		//Submit a secondary command
		void SubmitSecondary(Util::IntrusivePtr<CommandBuffer> secondary);
		//Returns the current subpassindex
		inline unsigned GetCurrentSubpass() const
		{
			return pipeline_state.subpass_index;
		}

		Util::IntrusivePtr<CommandBuffer> RequestSecondaryCommandBuffer(unsigned thread_index, unsigned subpass);
		static Util::IntrusivePtr<CommandBuffer> RequestSecondaryCommandBuffer(Device& device, const RenderPassInfo& rp, unsigned thread_index, unsigned subpass);

		void SetProgram(Program* program);

#ifdef QM_VULKAN_FILESYSTEM
		// Convenience functions for one-off shader binds.
		void set_program(const std::string& vertex, const std::string& fragment, const std::vector<std::pair<std::string, int>>& defines = {});
		void set_program(const std::string& compute, const std::vector<std::pair<std::string, int>>& defines = {});
#endif
		//-------------------Setting Uniforms----------------------------

		void SetBufferView(unsigned set, unsigned binding, const BufferView& view);
		void SetInputAttachments(unsigned set, unsigned start_binding);
		void SetTexture(unsigned set, unsigned binding, const ImageView& view);
		void SetUnormTexture(unsigned set, unsigned binding, const ImageView& view);
		void SetSrgbTexture(unsigned set, unsigned binding, const ImageView& view);
		void SetTexture(unsigned set, unsigned binding, const ImageView& view, const Sampler& sampler);
		void SetTexture(unsigned set, unsigned binding, const ImageView& view, StockSampler sampler);
		void SetStorageTexture(unsigned set, unsigned binding, const ImageView& view);
		void SetSampler(unsigned set, unsigned binding, const Sampler& sampler);
		void SetSampler(unsigned set, unsigned binding, StockSampler sampler);
		void SetUniformBuffer(unsigned set, unsigned binding, const Buffer& buffer);
		void SetUniformBuffer(unsigned set, unsigned binding, const Buffer& buffer, VkDeviceSize offset, VkDeviceSize range);
		void SetStorageBuffer(unsigned set, unsigned binding, const Buffer& buffer);
		void SetStorageBuffer(unsigned set, unsigned binding, const Buffer& buffer, VkDeviceSize offset, VkDeviceSize range);

		void SetBindless(unsigned set, VkDescriptorSet desc_set);

		void PushConstants(const void* data, VkDeviceSize offset, VkDeviceSize range);

		//-----------------------------------------------------------------

		//Allocates a uniform buffer from the command buffers internal pool. Binds it to the set and binding, than returns it's mapped data.
		void* AllocateConstantData(unsigned set, unsigned binding, VkDeviceSize size);
		//Allocates a uniform buffer from the command buffers internal pool. Binds it to the set and binding, than returns it's mapped data.
		template <typename T>
		T* AllocateTypedConstantData(unsigned set, unsigned binding, unsigned count)
		{
			return static_cast<T*>(AllocateConstantData(set, binding, count * sizeof(T)));
		}
		//Allocates a vertex buffer from the command buffers internal pool. Binds it to the binding than returns it's mapped data.
		void* AllocateVertexData(unsigned binding, VkDeviceSize size, VkDeviceSize stride, VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);
		//Allocates an index buffer from the command buffers internal pool. Binds then returns it's mapped data.
		void* AllocateIndexData(VkDeviceSize size, VkIndexType index_type);
		//Allocates a staging buffer from the command buffers internal pool. Returns a pointer to this staging buffer
		//Adds a CopyBuffer(staging, buffer) to the command buffer. Meaning that any memory writes to the host buffer (the
		//returned void*) must happen before the command is submitted (and cannot happen while its executing). Wait on 
		//TRANSFER stage to make writes to the image visible to other stages.
		void* UpdateBuffer(const Buffer& buffer, VkDeviceSize offset, VkDeviceSize size);
		//Allocates a staging buffer from command buffer's internal pool. Returns a pointer to this staging buffer.
		//Adds a CopyBufferToImage to the command buffer. Meaning that any memory writes to the host buffer (the
		//returned void*) must happen before the command is submitted (and cannot happen while its executing). 
		//Wait on TRANSFER stage to make writes to the image visible to other stages.
		void* UpdateImage(const Image& image, const VkOffset3D& offset, const VkExtent3D& extent, uint32_t row_length, uint32_t image_height, const VkImageSubresourceLayers& subresource);
		//Allocates a staging buffer from command buffer's internal pool. Returns a pointer to this staging buffer.
		//Adds a CopyBufferToImage to the command buffer. Meaning that any memory writes to the host buffer (the
		//returned void*) must happen before the command is submitted. Wait on TRANSFER stage to make writes to 
		//the image visible to other stages.
		void* UpdateImage(const Image& image, uint32_t row_length = 0, uint32_t image_height = 0);
		//Sets the viewport. Viewports and scissors are always dynamic, so this won't rereate the graphics pipeline
		void SetViewport(const VkViewport& viewport);
		const VkViewport& GetViewport() const;
		//Sets the scissor. Viewports and scissors are always dynamic, so this won't rereate the graphics pipeline
		void SetScissor(const VkRect2D& rect);
		//Sets a vertex attrbute to a specify format and size
		void SetVertexAttrib(uint32_t attrib, uint32_t binding, VkFormat format, VkDeviceSize offset);
		//Binds a buffer to a vertex binding
		void SetVertexBinding(uint32_t binding, const Buffer& buffer, VkDeviceSize offset, VkDeviceSize stride, VkVertexInputRate step_rate = VK_VERTEX_INPUT_RATE_VERTEX);
		//Bind an index buffer
		void SetIndexBuffer(const Buffer& buffer, VkDeviceSize offset, VkIndexType index_type);
		//Submit a draw call
		void Draw(uint32_t vertex_count, uint32_t instance_count = 1, uint32_t first_vertex = 0, uint32_t first_instance = 0);
		void DrawIndexed(uint32_t index_count, uint32_t instance_count = 1, uint32_t first_index = 0, int32_t vertex_offset = 0, uint32_t first_instance = 0);

		void Dispatch(uint32_t groups_x, uint32_t groups_y, uint32_t groups_z);
		void DispatchIndirect(const Buffer& buffer, uint32_t offset);

		void DrawIndirect(const Buffer& buffer, uint32_t offset, uint32_t draw_count, uint32_t stride);
		void DrawIndexedIndirect(const Buffer& buffer, uint32_t offset, uint32_t draw_count, uint32_t stride);
		void DrawMultiIndirect(const Buffer& buffer, uint32_t offset, uint32_t draw_count, uint32_t stride, const Buffer& count, uint32_t count_offset);
		void DrawIndexedMultiIndirect(const Buffer& buffer, uint32_t offset, uint32_t draw_count, uint32_t stride, const Buffer& count, uint32_t count_offset);
		//Culls back bit, disables blending, enables depth testing, triangle list topology.
		void SetOpaqueState();
		//No culling, disables blending, disables depth testing, triangle strip topology.
		void SetQuadState();
		//No culling, disables blending, enables depth testing, triangle strip topology.
		void SetOpaqueSpriteState();
		//No culling, src alpha blending, enables depth testing, triangle strip topology.
		void SetTransparentSpriteState();

		void SaveState(CommandBufferSaveStateFlags flags, CommandBufferSavedState& state);
		void RestoreState(const CommandBufferSavedState& state);

#define SET_STATIC_STATE(value)                               \
	do                                                        \
	{                                                         \
		if (pipeline_state.static_state.state.value != value) \
		{                                                     \
			pipeline_state.static_state.state.value = value;  \
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT); \
		}                                                     \
	} while (0)

#define SET_POTENTIALLY_STATIC_STATE(value)                       \
	do                                                            \
	{                                                             \
		if (pipeline_state.potential_static_state.value != value) \
		{                                                         \
			pipeline_state.potential_static_state.value = value;  \
			set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);     \
		}                                                         \
	} while (0)

		inline void SetDepthTest(bool depth_test, bool depth_write)
		{
			SET_STATIC_STATE(depth_test);
			SET_STATIC_STATE(depth_write);
		}

		inline void SetWireframe(bool wireframe)
		{
			SET_STATIC_STATE(wireframe);
		}

		inline void SetDepthCompare(VkCompareOp depth_compare)
		{
			SET_STATIC_STATE(depth_compare);
		}

		inline void SetBlendEnable(bool blend_enable)
		{
			SET_STATIC_STATE(blend_enable);
		}

		inline void SetBlendFactors(VkBlendFactor src_color_blend, VkBlendFactor src_alpha_blend, VkBlendFactor dst_color_blend, VkBlendFactor dst_alpha_blend)
		{
			SET_STATIC_STATE(src_color_blend);
			SET_STATIC_STATE(dst_color_blend);
			SET_STATIC_STATE(src_alpha_blend);
			SET_STATIC_STATE(dst_alpha_blend);
		}

		inline void SetBlendFactors(VkBlendFactor src_blend, VkBlendFactor dst_blend)
		{
			SetBlendFactors(src_blend, src_blend, dst_blend, dst_blend);
		}

		inline void SetBlendOp(VkBlendOp color_blend_op, VkBlendOp alpha_blend_op)
		{
			SET_STATIC_STATE(color_blend_op);
			SET_STATIC_STATE(alpha_blend_op);
		}

		inline void SetBlendOp(VkBlendOp blend_op)
		{
			SetBlendOp(blend_op, blend_op);
		}

		inline void SetDepthBias(bool depth_bias_enable)
		{
			SET_STATIC_STATE(depth_bias_enable);
		}

		inline void SetColorWriteMask(uint32_t write_mask)
		{
			SET_STATIC_STATE(write_mask);
		}

		inline void SetStencilTest(bool stencil_test)
		{
			SET_STATIC_STATE(stencil_test);
		}

		inline void SetStencilFrontOps(VkCompareOp stencil_front_compare_op, VkStencilOp stencil_front_pass, VkStencilOp stencil_front_fail, VkStencilOp stencil_front_depth_fail)
		{
			SET_STATIC_STATE(stencil_front_compare_op);
			SET_STATIC_STATE(stencil_front_pass);
			SET_STATIC_STATE(stencil_front_fail);
			SET_STATIC_STATE(stencil_front_depth_fail);
		}

		inline void SetStencilBackOps(VkCompareOp stencil_back_compare_op, VkStencilOp stencil_back_pass, VkStencilOp stencil_back_fail, VkStencilOp stencil_back_depth_fail)
		{
			SET_STATIC_STATE(stencil_back_compare_op);
			SET_STATIC_STATE(stencil_back_pass);
			SET_STATIC_STATE(stencil_back_fail);
			SET_STATIC_STATE(stencil_back_depth_fail);
		}

		inline void SetStencilOps(VkCompareOp stencil_compare_op, VkStencilOp stencil_pass, VkStencilOp stencil_fail,
			VkStencilOp stencil_depth_fail)
		{
			SetStencilFrontOps(stencil_compare_op, stencil_pass, stencil_fail, stencil_depth_fail);
			SetStencilBackOps(stencil_compare_op, stencil_pass, stencil_fail, stencil_depth_fail);
		}

		inline void SetPrimitiveTopology(VkPrimitiveTopology topology)
		{
			SET_STATIC_STATE(topology);
		}

		inline void SetPrimitiveRestart(bool primitive_restart)
		{
			SET_STATIC_STATE(primitive_restart);
		}

		inline void SetMultisampleState(bool alpha_to_coverage, bool alpha_to_one = false, bool sample_shading = false)
		{
			SET_STATIC_STATE(alpha_to_coverage);
			SET_STATIC_STATE(alpha_to_one);
			SET_STATIC_STATE(sample_shading);
		}

		inline void SetFrontFace(VkFrontFace front_face)
		{
			SET_STATIC_STATE(front_face);
		}

		inline void SetCullMode(VkCullModeFlags cull_mode)
		{
			SET_STATIC_STATE(cull_mode);
		}

		inline void SetBlendConstants(const float blend_constants[4])
		{
			SET_POTENTIALLY_STATIC_STATE(blend_constants[0]);
			SET_POTENTIALLY_STATIC_STATE(blend_constants[1]);
			SET_POTENTIALLY_STATIC_STATE(blend_constants[2]);
			SET_POTENTIALLY_STATIC_STATE(blend_constants[3]);
		}

		inline void SetSpecializationConstantMask(uint32_t spec_constant_mask)
		{
			VK_ASSERT((spec_constant_mask & ~((1u << VULKAN_NUM_SPEC_CONSTANTS) - 1u)) == 0u);
			SET_POTENTIALLY_STATIC_STATE(spec_constant_mask);
		}

		template <typename T>
		inline void SetSpecializationConstant(unsigned index, const T& value)
		{
			VK_ASSERT(index < VULKAN_NUM_SPEC_CONSTANTS);
			static_assert(sizeof(value) == sizeof(uint32_t), "Spec constant data must be 32-bit.");
			if (memcmp(&pipeline_state.potential_static_state.spec_constants[index], &value, sizeof(value)))
			{
				memcpy(&pipeline_state.potential_static_state.spec_constants[index], &value, sizeof(value));
				if (pipeline_state.potential_static_state.spec_constant_mask & (1u << index))
					set_dirty(COMMAND_BUFFER_DIRTY_STATIC_STATE_BIT);
			}
		}

		inline void EnableSubgroupSizeControl(bool subgroup_control_size)
		{
			SET_STATIC_STATE(subgroup_control_size);
		}

		inline void SetSubgroupSizeLog2(bool subgroup_full_group, uint8_t subgroup_minimum_size_log2, uint8_t subgroup_maximum_size_log2)
		{
			VK_ASSERT(subgroup_minimum_size_log2 < 8);
			VK_ASSERT(subgroup_maximum_size_log2 < 8);
			SET_STATIC_STATE(subgroup_full_group);
			SET_STATIC_STATE(subgroup_minimum_size_log2);
			SET_STATIC_STATE(subgroup_maximum_size_log2);
		}

		inline void SetConservativeRasterization(bool conservative_raster)
		{
			SET_STATIC_STATE(conservative_raster);
		}

#define SET_DYNAMIC_STATE(state, flags)   \
	do                                    \
	{                                     \
		if (dynamic_state.state != state) \
		{                                 \
			dynamic_state.state = state;  \
			set_dirty(flags);             \
		}                                 \
	} while (0)

		inline void SetDepthBias(float depth_bias_constant, float depth_bias_slope)
		{
			SET_DYNAMIC_STATE(depth_bias_constant, COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT);
			SET_DYNAMIC_STATE(depth_bias_slope, COMMAND_BUFFER_DIRTY_DEPTH_BIAS_BIT);
		}

		inline void SetStencilFrontReference(uint8_t front_compare_mask, uint8_t front_write_mask,
			uint8_t front_reference)
		{
			SET_DYNAMIC_STATE(front_compare_mask, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
			SET_DYNAMIC_STATE(front_write_mask, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
			SET_DYNAMIC_STATE(front_reference, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
		}

		inline void SetStencilBackReference(uint8_t back_compare_mask, uint8_t back_write_mask, uint8_t back_reference)
		{
			SET_DYNAMIC_STATE(back_compare_mask, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
			SET_DYNAMIC_STATE(back_write_mask, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
			SET_DYNAMIC_STATE(back_reference, COMMAND_BUFFER_DIRTY_STENCIL_REFERENCE_BIT);
		}

		inline void SetStencilReference(uint8_t compare_mask, uint8_t write_mask, uint8_t reference)
		{
			SetStencilFrontReference(compare_mask, write_mask, reference);
			SetStencilReference(compare_mask, write_mask, reference);
		}

		inline Type GetCommandBufferType() const
		{
			return type;
		}

		void End();

		void ExtractPipelineState(DeferredPipelineCompile& compile) const;
		static VkPipeline BuildGraphicsPipeline(Device* device, const DeferredPipelineCompile& compile);
		static VkPipeline BuildComputePipeline(Device* device, const DeferredPipelineCompile& compile);

		bool FlushPipelineStateWithoutBlocking();

	private:
		friend class Util::ObjectPool<CommandBuffer>;
		//Constructs the command buffer. Sets the device, table, cmd, cache and type. Also sets render state to opaque state.
		CommandBuffer(Device* device, VkCommandBuffer cmd, VkPipelineCache cache, Type type);
		//Device
		Device* device;
		//Volk Device Table
		const VolkDeviceTable& table;
		//Vulkan Handle
		VkCommandBuffer cmd;
		//Command Buffer type
		Type type;

		const Framebuffer* framebuffer = nullptr;
		const RenderPass* actual_render_pass = nullptr;
		const Vulkan::ImageView* framebuffer_attachments[VULKAN_NUM_ATTACHMENTS + 1] = {};

		IndexState index_state = {};
		VertexBindingState vbo = {};
		ResourceBindings bindings;
		VkDescriptorSet bindless_sets[VULKAN_NUM_DESCRIPTOR_SETS] = {};
		VkDescriptorSet allocated_sets[VULKAN_NUM_DESCRIPTOR_SETS] = {};

		VkPipeline current_pipeline = VK_NULL_HANDLE;
		VkPipelineLayout current_pipeline_layout = VK_NULL_HANDLE;
		PipelineLayout* current_layout = nullptr;
		VkSubpassContents current_contents = VK_SUBPASS_CONTENTS_INLINE;
		unsigned thread_index = 0;

		VkViewport viewport = {};
		VkRect2D scissor = {};

		CommandBufferDirtyFlags dirty = ~0u;
		uint32_t dirty_sets = 0;
		uint32_t dirty_sets_dynamic = 0;
		uint32_t dirty_vbos = 0;
		uint32_t active_vbos = 0;
		bool uses_swapchain = false;
		bool is_compute = true;
		bool is_secondary = false;

		void set_dirty(CommandBufferDirtyFlags flags)
		{
			dirty |= flags;
		}

		//Return which Flags are dirty (have been changed) clear the dirty mask.
		CommandBufferDirtyFlags get_and_clear(CommandBufferDirtyFlags flags)
		{
			auto mask = dirty & flags;
			dirty &= ~flags;
			return mask;
		}

		DeferredPipelineCompile pipeline_state = {};
		DynamicState dynamic_state = {};
#ifndef _MSC_VER
		static_assert(sizeof(pipeline_state.static_state.words) >= sizeof(pipeline_state.static_state.state),
			"Hashable pipeline state is not large enough!");
#endif

		bool FlushRenderState(bool synchronous);
		bool FlushComputeState(bool synchronous);
		void ClearRenderState();

		bool FlushGraphicsPipeline(bool synchronous);
		bool FlushComputePipeline(bool synchronous);
		void FlushDescriptorSets();
		void BeginGraphics();
		void FlushDescriptorSet(uint32_t set);
		void RebindDescriptorSet(uint32_t set);
		void BeginCompute();
		void BeginContext();

		BufferBlock vbo_block;
		BufferBlock ibo_block;
		BufferBlock ubo_block;
		BufferBlock staging_block;

		void SetTexture(unsigned set, unsigned binding, VkImageView float_view, VkImageView integer_view, VkImageLayout layout, uint64_t cookie);

		void InitViewportScissor(const RenderPassInfo& info, const Framebuffer* framebuffer);

		//Recalculates a graphics pipeline's hash
		static void UpdateHashGraphicsPipeline(DeferredPipelineCompile& compile, uint32_t& active_vbos);
		//Recalculates a compute pipeline's hash
		static void UpdateHashComputePipeline(DeferredPipelineCompile& compile);
	};

#ifdef QM_VULKAN_FILESYSTEM
	struct CommandBufferUtil
	{
		static void draw_fullscreen_quad(CommandBuffer& cmd, const std::string& vertex, const std::string& fragment,
			const std::vector<std::pair<std::string, int>>& defines = {});
		static void draw_fullscreen_quad_depth(CommandBuffer& cmd, const std::string& vertex, const std::string& fragment,
			bool depth_test, bool depth_write, VkCompareOp depth_compare,
			const std::vector<std::pair<std::string, int>>& defines = {});
		static void set_fullscreen_quad_vertex_state(CommandBuffer& cmd);
		static void set_quad_vertex_state(CommandBuffer& cmd);

		static void setup_fullscreen_quad(CommandBuffer& cmd, const std::string& vertex, const std::string& fragment,
			const std::vector<std::pair<std::string, int>>& defines = {},
			bool depth_test = false, bool depth_write = false,
			VkCompareOp depth_compare = VK_COMPARE_OP_ALWAYS);

		static void draw_fullscreen_quad(CommandBuffer& cmd, unsigned instances = 1);
		static void draw_quad(CommandBuffer& cmd, unsigned instances = 1);
	};
#endif

	using CommandBufferHandle = Util::IntrusivePtr<CommandBuffer>;

	constexpr int size = sizeof(CommandBuffer);
}
