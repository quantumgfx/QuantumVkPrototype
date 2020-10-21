#include "shader.hpp"
#include "quantumvk/vulkan/device.hpp"

#include <quantumvk/extern_build/spirv_cross_include.hpp>

using namespace std;
using namespace spirv_cross;
using namespace Util;

namespace Vulkan
{
	///////////////////////////////
	//Pipeline Layout//////////////
	///////////////////////////////

	ProgramLayout::ProgramLayout(Device* device)
		: device(device)
	{
	}

	ProgramLayout::~ProgramLayout()
	{
	}

	void ProgramLayout::InitLayout(Program& program)
	{
		if (program.HasShader(ShaderStage::Vertex))
			attribute_mask = program.GetShader(ShaderStage::Vertex)->GetLayout().input_mask;
		if (program.HasShader(ShaderStage::Fragment))
			render_target_mask = program.GetShader(ShaderStage::Fragment)->GetLayout().output_mask;

		// Set descriptor set layout, stages and push constant info
		for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
		{
			ShaderStage shader_type = static_cast<ShaderStage>(i);
			if (!program.HasShader(shader_type))
				continue;

			auto& shader = program.GetShader(shader_type);

			uint32_t stage_mask = 1u << i;

			const auto& shader_layout = shader->GetLayout();
				

			spec_constant_mask[i] = shader_layout.spec_constant_mask;
			combined_spec_constant_mask |= shader_layout.spec_constant_mask;
		}

		uniforms.InitUniforms(device, program);

	}

	////////////////////////////////
	//Shader////////////////////////
	////////////////////////////////

	const char* Shader::StageToName(ShaderStage stage)
	{
		switch (stage)
		{
		case ShaderStage::Compute:
			return "compute";
		case ShaderStage::Vertex:
			return "vertex";
		case ShaderStage::Fragment:
			return "fragment";
		case ShaderStage::Geometry:
			return "geometry";
		case ShaderStage::TessControl:
			return "tess_control";
		case ShaderStage::TessEvaluation:
			return "tess_evaluation";
		default:
			return "unknown";
		}
	}

	VkShaderStageFlagBits Shader::StageToVkType(ShaderStage stage)
	{
		switch (stage)
		{
		case ShaderStage::Compute:
			return VK_SHADER_STAGE_COMPUTE_BIT;
		case ShaderStage::Vertex:
			return VK_SHADER_STAGE_VERTEX_BIT;
		case ShaderStage::Fragment:
			return VK_SHADER_STAGE_FRAGMENT_BIT;
		case ShaderStage::Geometry:
			return VK_SHADER_STAGE_GEOMETRY_BIT;
		case ShaderStage::TessControl:
			return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		case ShaderStage::TessEvaluation:
			return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		default:
			return VK_SHADER_STAGE_VERTEX_BIT;
		}
	}

	static bool get_stock_sampler(StockSampler& sampler, const string& name)
	{
		if (name.find("NearestClamp") != string::npos)
			sampler = StockSampler::NearestClamp;
		else if (name.find("LinearClamp") != string::npos)
			sampler = StockSampler::LinearClamp;
		else if (name.find("TrilinearClamp") != string::npos)
			sampler = StockSampler::TrilinearClamp;
		else if (name.find("NearestWrap") != string::npos)
			sampler = StockSampler::NearestWrap;
		else if (name.find("LinearWrap") != string::npos)
			sampler = StockSampler::LinearWrap;
		else if (name.find("TrilinearWrap") != string::npos)
			sampler = StockSampler::TrilinearWrap;
		else if (name.find("NearestShadow") != string::npos)
			sampler = StockSampler::NearestShadow;
		else if (name.find("LinearShadow") != string::npos)
			sampler = StockSampler::LinearShadow;
		else
			return false;

		return true;
	}

	void Shader::UpdateArrayInfo(const SPIRType& type, unsigned set, unsigned binding)
	{
		auto& size = layout.sets[set].array_size[binding];
		if (!type.array.empty())
		{
			if (type.array.size() != 1)
				QM_LOG_ERROR("Array dimension must be 1.\n");
			else if (!type.array_size_literal.front())
				QM_LOG_ERROR("Array dimension must be a literal.\n");
			else
			{
				//if (type.array.front() == 0)
				//{
				//	// Runtime array.
				//	if (!device->GetDeviceExtensions().supports_descriptor_indexing)
				//		QM_LOG_ERROR("Sufficient features for descriptor indexing is not supported on this device.\n");

				//	if (binding != 0)
				//		QM_LOG_ERROR("Bindless textures can only be used with binding = 0 in a set.\n");

				//	if (type.basetype != SPIRType::Image || type.image.dim == spv::DimBuffer)
				//		QM_LOG_ERROR("Can only use bindless for sampled images.\n");
				//	else
				//		layout.bindless_set_mask |= 1u << set;

				//	size = DescriptorSetLayout::UNSIZED_ARRAY;
				//}
				//else 
				if (size && size != type.array.front())
					QM_LOG_ERROR("Array dimension for (%u, %u) is inconsistent.\n", set, binding);
				else if (type.array.front() + binding > VULKAN_NUM_BINDINGS)
					QM_LOG_ERROR("Binding array will go out of bounds.\n");
				else
					size = uint8_t(type.array.front());
			}
		}
		else
		{
			if (size && size != 1)
				QM_LOG_ERROR("Array dimension for (%u, %u) is inconsistent.\n", set, binding);
			size = 1;
		}
	}

	Shader::Shader(Device* device_, const uint32_t* data, size_t size)
		: device(device_)
	{
		// Compute shader hash
		Util::Hasher hasher;
		hasher.data(data, size);
		hash = hasher.get();
		// -------------------


		VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		info.codeSize = size;
		info.pCode = data;

#ifdef VULKAN_DEBUG
		QM_LOG_INFO("Creating shader module.\n");
#endif
		auto& table = device->GetDeviceTable();
		if (table.vkCreateShaderModule(device->GetDevice(), &info, nullptr, &module) != VK_SUCCESS)
			QM_LOG_ERROR("Failed to create shader module.\n");

		Compiler compiler(data, size / sizeof(uint32_t));

		auto resources = compiler.get_shader_resources();
		for (auto& image : resources.sampled_images)
		{
			auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
			layout.set_mask |= 1u << set;

			auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
			auto& type = compiler.get_type(image.type_id);
			if (type.image.dim == spv::DimBuffer)
				layout.sets[set].sampled_buffer_mask |= 1u << binding;
			else
				layout.sets[set].sampled_image_mask |= 1u << binding;

			if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
				layout.sets[set].fp_mask |= 1u << binding;

			const string& name = image.name;
			StockSampler sampler;
			if (type.image.dim != spv::DimBuffer && get_stock_sampler(sampler, name))
			{
				if (HasImmutableSampler(layout.sets[set], binding))
				{
					if (sampler != GetImmutableSampler(layout.sets[set], binding))
						QM_LOG_ERROR("Immutable sampler mismatch detected!\n");
				}
				else
					SetImmutableSampler(layout.sets[set], binding, sampler);
			}

			UpdateArrayInfo(type, set, binding);
		}

		for (auto& image : resources.subpass_inputs)
		{
			auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
			layout.set_mask |= 1u << set;

			auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
			layout.sets[set].input_attachment_mask |= 1u << binding;

			auto& type = compiler.get_type(image.type_id);
			if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
				layout.sets[set].fp_mask |= 1u << binding;
			UpdateArrayInfo(type, set, binding);
		}

		for (auto& image : resources.separate_images)
		{
			auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
			layout.set_mask |= 1u << set;

			auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);

			auto& type = compiler.get_type(image.type_id);
			if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
				layout.sets[set].fp_mask |= 1u << binding;

			if (type.image.dim == spv::DimBuffer)
				layout.sets[set].sampled_buffer_mask |= 1u << binding;
			else
				layout.sets[set].separate_image_mask |= 1u << binding;

			UpdateArrayInfo(type, set, binding);
		}

		for (auto& image : resources.separate_samplers)
		{
			auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
			layout.set_mask |= 1u << set;

			auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
			layout.sets[set].sampler_mask |= 1u << binding;

			const string& name = image.name;
			StockSampler sampler;
			if (get_stock_sampler(sampler, name))
			{
				if (HasImmutableSampler(layout.sets[set], binding))
				{
					if (sampler != GetImmutableSampler(layout.sets[set], binding))
						QM_LOG_ERROR("Immutable sampler mismatch detected!\n");
				}
				else
					SetImmutableSampler(layout.sets[set], binding, sampler);
			}

			UpdateArrayInfo(compiler.get_type(image.type_id), set, binding);
		}

		for (auto& image : resources.storage_images)
		{
			auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
			layout.set_mask |= 1u << set;

			auto binding = compiler.get_decoration(image.id, spv::DecorationBinding);
			layout.sets[set].storage_image_mask |= 1u << binding;

			auto& type = compiler.get_type(image.type_id);
			if (compiler.get_type(type.image.type).basetype == SPIRType::BaseType::Float)
				layout.sets[set].fp_mask |= 1u << binding;

			UpdateArrayInfo(type, set, binding);
		}

		for (auto& buffer : resources.uniform_buffers)
		{
			auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
			layout.set_mask |= 1u << set;

			auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
			layout.sets[set].uniform_buffer_mask |= 1u << binding;
			UpdateArrayInfo(compiler.get_type(buffer.type_id), set, binding);
		}

		for (auto& buffer : resources.storage_buffers)
		{
			auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
			layout.set_mask |= 1u << set;

			auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
			layout.sets[set].storage_buffer_mask |= 1u << binding;
			UpdateArrayInfo(compiler.get_type(buffer.type_id), set, binding);
		}

		for (auto& attrib : resources.stage_inputs)
		{
			auto location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
			layout.input_mask |= 1u << location;
		}

		for (auto& attrib : resources.stage_outputs)
		{
			auto location = compiler.get_decoration(attrib.id, spv::DecorationLocation);
			layout.output_mask |= 1u << location;
		}

		if (!resources.push_constant_buffers.empty())
		{
			// Don't bother trying to extract which part of a push constant block we're using.
			// Just assume we're accessing everything. At least on older validation layers,
			// it did not do a static analysis to determine similar information, so we got a lot
			// of false positives.
			layout.push_constant_size = compiler.get_declared_struct_size(compiler.get_type(resources.push_constant_buffers.front().base_type_id));
		}

		auto spec_constants = compiler.get_specialization_constants();
		for (auto& c : spec_constants)
		{
			if (c.constant_id >= VULKAN_NUM_SPEC_CONSTANTS)
			{
				QM_LOG_ERROR("Spec constant ID: %u is out of range, will be ignored.\n", c.constant_id);
				continue;
			}

			layout.spec_constant_mask |= 1u << c.constant_id;
		}
	}

	Shader::~Shader()
	{
		auto& table = device->GetDeviceTable();
		// Module can be deleted here. Shaders are just used to create pipelines, so if the reference to the shader is dropped it will never again be needed to create a pipeline.
		table.vkDestroyShaderModule(device->GetDevice(), module, nullptr);
	}

	void ShaderDeleter::operator()(Shader* shader)
	{
		shader->device->handle_pool.shaders.free(shader);
	}

	////////////////////////
	//Program///////////////
	////////////////////////

	Program::Program(Device* device_, const GraphicsProgramShaders& graphics_shaders)
		: device(device_), program_layout(device_)
	{
		VK_ASSERT(graphics_shaders.vertex);

#ifdef VULKAN_DEBUG
		QM_LOG_INFO("Creating graphics program.\n");
#endif
		// Compute program hash
		Util::Hasher hasher;
		hasher.u64(graphics_shaders.vertex->GetHash());
		if (graphics_shaders.tess_control)
			hasher.u64(graphics_shaders.tess_control->GetHash());
		if (graphics_shaders.tess_eval)
			hasher.u64(graphics_shaders.tess_eval->GetHash());
		if (graphics_shaders.geometry)
			hasher.u64(graphics_shaders.geometry->GetHash());
		if (graphics_shaders.fragment)
			hasher.u64(graphics_shaders.fragment->GetHash());
		hash = hasher.get();
		// --------------------

		// Check that certain shaders are supported
		if ((graphics_shaders.tess_eval || graphics_shaders.tess_control) && !device_->GetDeviceFeatures().tessellationShader)
			QM_LOG_ERROR("Tesselation shaders used but gpu doesn't support tesselation");
		if (graphics_shaders.geometry && !device_->GetDeviceFeatures().geometryShader)
			QM_LOG_ERROR("Geometry shaders used but gpu doesn't support geometry shaders");

		shaders = graphics_shaders;
		program_layout.InitLayout(*this);
	}

	Program::Program(Device* device_, const ComputeProgramShaders& compute_shaders)
		: device(device_), program_layout(device_)
	{
		VK_ASSERT(compute_shaders.compute);

#ifdef VULKAN_DEBUG
		QM_LOG_INFO("Creating compute program.\n");
#endif
		// Compute program hash
		Util::Hasher hasher;
		hasher.u64(compute_shaders.compute->GetHash());
		hash = hasher.get();
		// --------------------

		shaders = compute_shaders;
		program_layout.InitLayout(*this);
	}

	VkPipeline Program::GetPipeline(Hash hash) const
	{
		auto* ret = pipelines.find(hash);
		return ret ? ret->get() : VK_NULL_HANDLE;
	}

	VkPipeline Program::AddPipeline(Hash hash, VkPipeline pipeline)
	{
		return pipelines.emplace_yield(hash, pipeline)->get();
	}

	void Program::BeginFrame()
	{
		program_layout.GetUniformManager().BeginFrame();
	}

	void Program::Clear()
	{
		program_layout.GetUniformManager().Clear();
	}

	Program::~Program()
	{
#ifdef VULKAN_DEBUG
		QM_LOG_INFO("Destroying program\n");
#endif
		auto& table = device->GetDeviceTable();
		for (auto& pipe : pipelines)
			table.vkDestroyPipeline(device->GetDevice(), pipe.get(), nullptr);
	}

	void ProgramDeleter::operator()(Program* program)
	{
		program->device->DestroyProgramNoLock(program);
	}
	
}
