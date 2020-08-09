#pragma once

#include "cookie.hpp"
#include "descriptor_set.hpp"
#include "utils/hash.hpp"
#include "utils/intrusive.hpp"
#include "limits.hpp"
#include "vulkan_headers.hpp"
#include "utils/enum_cast.hpp"

namespace spirv_cross
{
	struct SPIRType;
}

namespace Vulkan
{
	class Device;

	// Different types of shader stages
	enum class ShaderStage
	{
		Vertex = 0,
		TessControl = 1,
		TessEvaluation = 2,
		Geometry = 3,
		Fragment = 4,
		Compute = 5,
		Count
	};

	// Specifies the layout of resources in a shader
	struct ResourceLayout
	{
		uint32_t input_mask = 0;
		uint32_t output_mask = 0;
		uint32_t push_constant_size = 0;
		uint32_t spec_constant_mask = 0;
		uint32_t bindless_set_mask = 0;
		DescriptorSetLayout sets[VULKAN_NUM_DESCRIPTOR_SETS];
	};

	struct CombinedResourceLayout
	{
		uint32_t attribute_mask = 0;
		uint32_t render_target_mask = 0;
		DescriptorSetLayout sets[VULKAN_NUM_DESCRIPTOR_SETS] = {};
		uint32_t stages_for_bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};
		uint32_t stages_for_sets[VULKAN_NUM_DESCRIPTOR_SETS] = {};
		VkPushConstantRange push_constant_range = {};
		uint32_t descriptor_set_mask = 0;
		uint32_t bindless_descriptor_set_mask = 0;
		uint32_t spec_constant_mask[Util::ecast(ShaderStage::Count)] = {};
		uint32_t combined_spec_constant_mask = 0;
		Util::Hash push_constant_layout_hash = 0;
	};

	struct ResourceBinding
	{
		union {
			VkDescriptorBufferInfo buffer;
			struct
			{
				VkDescriptorImageInfo fp;
				VkDescriptorImageInfo integer;
			} image;
			VkBufferView buffer_view;
		};
		VkDeviceSize dynamic_offset;
	};

	struct ResourceBindings
	{
		ResourceBinding bindings[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
		uint64_t cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
		uint64_t secondary_cookies[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS];
		uint8_t push_constant_data[VULKAN_PUSH_CONSTANT_SIZE];
	};

	class PipelineLayout : public HashedObject<PipelineLayout>
	{
	public:
		PipelineLayout(Util::Hash hash, Device* device, const CombinedResourceLayout& layout);
		~PipelineLayout();

		const CombinedResourceLayout& GetResourceLayout() const
		{
			return layout;
		}

		VkPipelineLayout GetLayout() const
		{
			return pipe_layout;
		}

		DescriptorSetAllocator* GetAllocator(unsigned set) const
		{
			return set_allocators[set];
		}

		VkDescriptorUpdateTemplateKHR GetUpdateTemplate(unsigned set) const
		{
			return update_template[set];
		}

	private:
		Device* device;
		VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
		CombinedResourceLayout layout;
		DescriptorSetAllocator* set_allocators[VULKAN_NUM_DESCRIPTOR_SETS] = {};
		VkDescriptorUpdateTemplateKHR update_template[VULKAN_NUM_DESCRIPTOR_SETS] = {};
		void CreateUpdateTemplates();
	};

	// Essentially just a vkShaderModule
	class Shader : public HashedObject<Shader>
	{
	public:
		Shader(Util::Hash hash, Device* device, const uint32_t* data, size_t size);
		~Shader();

		const ResourceLayout& GetLayout() const
		{
			return layout;
		}

		VkShaderModule GetModule() const
		{
			return module;
		}

		static const char* StageToName(ShaderStage stage);

	private:
		Device* device;
		VkShaderModule module;
		ResourceLayout layout;

		void UpdateArrayInfo(const spirv_cross::SPIRType& type, unsigned set, unsigned binding);
	};

	// Represents multiple shaders bound together into a sequence. Contains pipeline layout, shaders and pipeline cache (all the possible different combinations of state info).
	// The actual pipelines are created in the command buffers
	class Program : public HashedObject<Program>, public InternalSyncEnabled
	{
	public:
		Program(Device* device, Shader* vertex, Shader* fragment);
		Program(Device* device, Shader* compute);
		~Program();

		inline const Shader* GetShader(ShaderStage stage) const
		{
			return shaders[Util::ecast(stage)];
		}

		void SetPipelineLayout(PipelineLayout* new_layout)
		{
			layout = new_layout;
		}

		PipelineLayout* GetPipelineLayout() const
		{
			return layout;
		}

		VkPipeline GetPipeline(Util::Hash hash) const;
		VkPipeline AddPipeline(Util::Hash hash, VkPipeline pipeline);

	private:
		void SetShader(ShaderStage stage, Shader* handle);
		Device* device;
		Shader* shaders[Util::ecast(ShaderStage::Count)] = {};
		PipelineLayout* layout = nullptr;
		VulkanCache<Util::IntrusivePODWrapper<VkPipeline>> pipelines;
	};

}
