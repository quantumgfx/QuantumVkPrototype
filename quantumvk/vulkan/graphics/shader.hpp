#pragma once

#include "quantumvk/utils/hash.hpp"
#include "quantumvk/utils/intrusive.hpp"
#include "quantumvk/utils/enum_cast.hpp"
#include "quantumvk/utils/object_pool.hpp"

#include "descriptor_set.hpp"

#include "quantumvk/vulkan/misc/cookie.hpp"
#include "quantumvk/vulkan/misc/limits.hpp"
#include "quantumvk/vulkan/vulkan_headers.hpp"

#include <variant>

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

	enum class ProgramType
	{
		Graphics = 0,
		Compute
	};

	// Specifies the layout of resources in a signle shader module
	struct ResourceLayout
	{
		uint32_t input_mask = 0;
		uint32_t output_mask = 0;
		uint32_t push_constant_size = 0;
		uint32_t spec_constant_mask = 0;
		//uint32_t bindless_set_mask = 0;
		uint32_t set_mask = 0;
		DescriptorSetLayout sets[VULKAN_NUM_DESCRIPTOR_SETS];
	};

	// Represents a single descriptor in a descriptor set. 
	struct ResourceBinding
	{
		union 
		{
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

	class Shader;

	struct ShaderDeleter
	{
		void operator()(Shader* shader);
	};

	// Essentially just a vkShaderModule
	class Shader : public Util::IntrusivePtrEnabled<Shader, ShaderDeleter, HandleCounter>
	{
	public:

		friend struct ShaderDeleter;
		~Shader();

		const ResourceLayout& GetLayout() const
		{
			return layout;
		}

		VkShaderModule GetModule() const
		{
			return module;
		}

		Util::Hash GetHash() const
		{
			return hash;
		}

		static const char* StageToName(ShaderStage stage);

	private:

		friend class Util::ObjectPool<Shader>;

		Shader(Device* device, const uint32_t* data, size_t size);

		Util::Hash hash;

		Device* device;
		VkShaderModule module;
		ResourceLayout layout;

		void UpdateArrayInfo(const spirv_cross::SPIRType& type, unsigned set, unsigned binding);
	};

	using ShaderHandle = Util::IntrusivePtr<Shader>;

	// Contains extra infomation, wrapping a ResourceBinding
	struct UniformBinding
	{
		// Resource bound to this uniform
		ResourceBinding resource;
		// Primary object cookie
		uint64_t cookie = 0;
		// Secondary object cookie (for example: sampler)
		uint64_t secondary_cookie = 0;
	};

	struct PerDescriptorSet
	{
		uint32_t stages = 0;
		uint32_t stages_for_bindings[VULKAN_NUM_BINDINGS] = {};
		DescriptorSetLayout set_layout = {};
		DescriptorSetAllocator* set_allocator = nullptr;
		VkDescriptorUpdateTemplateKHR update_template;

		// Descriptors

		// Offset into the descriptor array for each binding
		uint32_t offset[VULKAN_NUM_BINDINGS] = {};
		uint32_t descriptor_count = 0;
		UniformBinding* descriptor_array = nullptr;

		void SetDescriptor(uint32_t binding, uint32_t array_index, const UniformBinding& resource)
		{
			*(descriptor_array + offset[binding] + array_index) = resource;
		}

		UniformBinding& GetDescriptor(uint32_t binding, uint32_t array_index) const
		{
			return *(descriptor_array + offset[binding] + array_index);
		}
	};

	// Forward declaration
	class Program;

	// Contains imformation about the resources used by a program.
	class ProgramLayout
	{

	public:

		ProgramLayout(Device* device);
		~ProgramLayout();

		VkPipelineLayout GetVkLayout() const { return vklayout; }
		// Masks
		uint32_t GetAttribMask() const { return attribute_mask; }
		uint32_t GetRenderTargetMask() const { return render_target_mask; }
		uint32_t GetSpecConstantMask(ShaderStage stage) const { return spec_constant_mask[static_cast<uint32_t>(stage)]; }
		uint32_t GetCombindedSpecConstantMask() const { return combined_spec_constant_mask; }
		uint32_t GetDescriptorSetMask() const { return descriptor_set_mask; }
		// Push constants
		const VkPushConstantRange& GetPushConstantRange() const { return push_constant_range; }
		// Creates the layout
		void CreateLayout(Program& program);
		void DestroyLayout();

		// Returns a particular descriptor set
		PerDescriptorSet* GetDescriptorSet(uint32_t set) const { return per_set_array + set; }

		// Returns whether a descriptor set is active
		bool HasDescriptorSet(uint32_t set) const { return (descriptor_set_mask & (1u << set)); }
		// Returns whether a descriptor binding is active
		bool HasDescriptorBinding(uint32_t set, uint32_t binding) const { return GetDescriptorSet(set)->stages_for_bindings[binding] != 0; }

		// Returns the number of descriptors a set has
		uint32_t GetDescriptorCount(uint32_t set) const { return GetDescriptorSet(set)->descriptor_count; };
		// Returns the array size of a particular binding
		uint32_t GetArraySize(uint32_t set, uint32_t binding) const { return GetDescriptorSet(set)->set_layout.array_size[binding]; }
		// Returns the descriptor stored at a particular (set, binding, array_index)
		UniformBinding& GetDescriptor(uint32_t set, uint32_t binding, uint32_t array_index) const { return GetDescriptorSet(set)->GetDescriptor(binding, array_index); }

		// Descriptor Sets
		VkDescriptorSet FlushDescriptorSet(uint32_t thread_index, uint32_t set);

		void ResetDescriptorSets();

	private:

		void CreateUpdateTemplates();
		void UpdateDescriptorSetLegacy(VkDescriptorSet desc_set, const DescriptorSetLayout& set_layout, uint32_t set);

		Device* device;

		VkPipelineLayout vklayout = VK_NULL_HANDLE;
		uint32_t attribute_mask = 0;
		uint32_t render_target_mask = 0;
		uint32_t spec_constant_mask[Util::ecast(ShaderStage::Count)] = {};
		uint32_t combined_spec_constant_mask = 0;

		uint32_t descriptor_set_mask = 0;
		//uint32_t bindless_descriptor_set_mask = 0;

		uint32_t desc_set_count = 0;
		PerDescriptorSet* per_set_array = nullptr;
		uint32_t descriptor_count = 0;
		UniformBinding* descriptor_array = nullptr;

		VkPushConstantRange push_constant_range = {};

	};

	// TODO make sure every loop involving VULKAN_NUM_DESCRIPTOR_SETS checks that the layout is valid first

	// Functor to delete Program
	struct ProgramDeleter
	{
		void operator()(Program* program);
	};

	// Modules to create graphics program
	struct GraphicsProgramShaders
	{
		ShaderHandle vertex;
		ShaderHandle tess_control;
		ShaderHandle tess_eval;
		ShaderHandle geometry;
		ShaderHandle fragment;
	};

	// Modules to crate compute program
	struct ComputeProgramShaders
	{
		ShaderHandle compute;
	};

	// Represents multiple shaders bound together into a sequence. Contains pipeline layout, shaders, descriptor_info, and pipeline_cache (all the possible different combinations of state info).
	// The actual pipelines are created in the command buffers. 
	class Program : public Util::IntrusivePtrEnabled<Program, ProgramDeleter, HandleCounter>
	{
	public:

		friend struct ProgramDeleter;

		~Program();

		bool HasShader(ShaderStage stage) const
		{
			if (shaders.index() == 0)
			{
				auto& stages = std::get<0>(shaders);
				if (stage == ShaderStage::Vertex)
					return stages.vertex;
				if (stage == ShaderStage::Fragment)
					return stages.fragment;
				if (stage == ShaderStage::TessControl)
					return stages.tess_control;
				if (stage == ShaderStage::TessEvaluation)
					return stages.tess_eval;
				if (stage == ShaderStage::Geometry)
					return stages.geometry;
			}
			if (shaders.index() == 1)
			{
				auto& stages = std::get<1>(shaders);
				if (stage == ShaderStage::Compute)
					return stages.compute;
			}
			return false;
		}

		ShaderHandle GetShader(ShaderStage stage) const
		{
			switch (stage)
			{
			case Vulkan::ShaderStage::Vertex:         VK_ASSERT(shaders.index() == 0); return std::get<0>(shaders).vertex;
			case Vulkan::ShaderStage::TessControl:    VK_ASSERT(shaders.index() == 0); return std::get<0>(shaders).tess_control;
			case Vulkan::ShaderStage::TessEvaluation: VK_ASSERT(shaders.index() == 0); return std::get<0>(shaders).tess_eval;
			case Vulkan::ShaderStage::Geometry:       VK_ASSERT(shaders.index() == 0); return std::get<0>(shaders).geometry;
			case Vulkan::ShaderStage::Fragment:       VK_ASSERT(shaders.index() == 0); return std::get<0>(shaders).fragment;
			case Vulkan::ShaderStage::Compute:        VK_ASSERT(shaders.index() == 1); return std::get<1>(shaders).compute;
			default: return ShaderHandle{ nullptr };
			}
		}

		ProgramType GetProgramType()
		{
			return shaders.index() == 0 ? ProgramType::Graphics : ProgramType::Compute;
		}

		Util::Hash GetHash() const
		{
			return hash;
		}

		ProgramLayout& GetLayout()
		{
			return program_layout;
		}

		VkPipeline GetPipeline(Util::Hash hash) const;
		VkPipeline AddPipeline(Util::Hash hash, VkPipeline pipeline);

		void ResetUniforms()
		{
			program_layout.ResetDescriptorSets();
		}

	private:

		friend class Util::ObjectPool<Program>;

		// Create graphics program
		Program(Device* device, const GraphicsProgramShaders& graphics_shaders);
		// Compute pipeline
		Program(Device* device, const ComputeProgramShaders& compute_shaders);

	private:

		Util::Hash hash;
		Device* device;

		std::variant<GraphicsProgramShaders, ComputeProgramShaders> shaders;

		VulkanCache<Util::IntrusivePODWrapper<VkPipeline>> pipelines;

		ProgramLayout program_layout;
	};

	using ProgramHandle = Util::IntrusivePtr<Program>;
}
