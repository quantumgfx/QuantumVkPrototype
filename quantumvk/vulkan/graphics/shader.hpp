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

	class Shader;

	struct ShaderDeleter
	{
		void operator()(Shader* shader);
	};

	// Essentially just a ref counted VkShaderModule
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
		static VkShaderStageFlagBits StageToVkType(ShaderStage stage);

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

	// Forward declaration
	class Program;

	// Contains imformation about the resources used by a program.
	class ProgramLayout
	{

	public:

		ProgramLayout(Device* device);
		~ProgramLayout();

		// Masks
		uint32_t GetAttribMask() const { return attribute_mask; }
		uint32_t GetRenderTargetMask() const { return render_target_mask; }
		uint32_t GetSpecConstantMask(ShaderStage stage) const { return spec_constant_mask[static_cast<uint32_t>(stage)]; }
		uint32_t GetCombindedSpecConstantMask() const { return combined_spec_constant_mask; }
		UniformManager& GetUniformManager() { return uniforms; }

		// Creates the layout
		void InitLayout(Program& program);

	private:

		Device* device;

		uint32_t attribute_mask = 0;
		uint32_t render_target_mask = 0;
		uint32_t spec_constant_mask[Util::ecast(ShaderStage::Count)] = {};
		uint32_t combined_spec_constant_mask = 0;

		UniformManager uniforms;

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
			else if (shaders.index() == 1)
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

		ProgramType GetProgramType() { return shaders.index() == 0 ? ProgramType::Graphics : ProgramType::Compute; }

		Util::Hash GetHash() const { return hash; }
		ProgramLayout& GetLayout() { return program_layout; }
		UniformManager& GetUniforms() { return program_layout.GetUniformManager(); }

		VkPipeline GetPipeline(Util::Hash hash) const;
		VkPipeline AddPipeline(Util::Hash hash, VkPipeline pipeline);

		void BeginFrame();
		void Clear();

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
