#include "shader.hpp"
#include "device.hpp"
#include "spirv_cross.hpp"

using namespace std;
using namespace spirv_cross;
using namespace Util;

namespace Vulkan
{
	PipelineLayout::PipelineLayout(Hash hash, Device* device_, const CombinedResourceLayout& layout_)
		: IntrusiveHashMapEnabled<PipelineLayout>(hash)
		, device(device_)
		, layout(layout_)
	{

		VkDescriptorSetLayout layouts[VULKAN_NUM_DESCRIPTOR_SETS] = {};
		unsigned num_sets = 0;
		for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
		{
			set_allocators[i] = device->RequestDescriptorSetAllocator(layout.sets[i], layout.stages_for_bindings[i]);
			layouts[i] = set_allocators[i]->GetLayout();
			if (layout.descriptor_set_mask & (1u << i))
				num_sets = i + 1;
		}

		if (num_sets > device->GetGPUProperties().limits.maxBoundDescriptorSets)
		{
			QM_LOG_ERROR("Number of sets %u exceeds device limit of %u.\n",
				num_sets, device->GetGPUProperties().limits.maxBoundDescriptorSets);
		}

		VkPipelineLayoutCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		if (num_sets)
		{
			info.setLayoutCount = num_sets;
			info.pSetLayouts = layouts;
		}

		if (layout.push_constant_range.stageFlags != 0)
		{
			info.pushConstantRangeCount = 1;
			info.pPushConstantRanges = &layout.push_constant_range;
		}

#ifdef VULKAN_DEBUG
		QM_LOG_INFO("Creating pipeline layout.\n");
#endif
		auto& table = device->GetDeviceTable();
		if (table.vkCreatePipelineLayout(device->GetDevice(), &info, nullptr, &pipe_layout) != VK_SUCCESS)
			QM_LOG_ERROR("Failed to create pipeline layout.\n");

		device->register_pipeline_layout(pipe_layout, get_hash(), info);

		if (device->GetDeviceFeatures().supports_update_template)
			CreateUpdateTemplates();
	}

	void PipelineLayout::CreateUpdateTemplates()
	{
		auto& table = device->GetDeviceTable();
		for (unsigned desc_set = 0; desc_set < VULKAN_NUM_DESCRIPTOR_SETS; desc_set++)
		{
			if ((layout.descriptor_set_mask & (1u << desc_set)) == 0)
				continue;
			if ((layout.bindless_descriptor_set_mask & (1u << desc_set)) == 0)
				continue;

			VkDescriptorUpdateTemplateEntryKHR update_entries[VULKAN_NUM_BINDINGS];
			uint32_t update_count = 0;

			auto& set_layout = layout.sets[desc_set];

			for_each_bit(set_layout.uniform_buffer_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = offsetof(ResourceBinding, buffer) + sizeof(ResourceBinding) * binding;
				entry.stride = sizeof(ResourceBinding);
				});

			for_each_bit(set_layout.storage_buffer_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = offsetof(ResourceBinding, buffer) + sizeof(ResourceBinding) * binding;
				entry.stride = sizeof(ResourceBinding);
				});

			for_each_bit(set_layout.sampled_buffer_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = offsetof(ResourceBinding, buffer_view) + sizeof(ResourceBinding) * binding;
				entry.stride = sizeof(ResourceBinding);
				});

			for_each_bit(set_layout.sampled_image_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				if (set_layout.fp_mask & (1u << binding))
					entry.offset = offsetof(ResourceBinding, image.fp) + sizeof(ResourceBinding) * binding;
				else
					entry.offset = offsetof(ResourceBinding, image.integer) + sizeof(ResourceBinding) * binding;
				entry.stride = sizeof(ResourceBinding);
				});

			for_each_bit(set_layout.separate_image_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				if (set_layout.fp_mask & (1u << binding))
					entry.offset = offsetof(ResourceBinding, image.fp) + sizeof(ResourceBinding) * binding;
				else
					entry.offset = offsetof(ResourceBinding, image.integer) + sizeof(ResourceBinding) * binding;
				entry.stride = sizeof(ResourceBinding);
				});

			for_each_bit(set_layout.sampler_mask & ~set_layout.immutable_sampler_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = offsetof(ResourceBinding, image.fp) + sizeof(ResourceBinding) * binding;
				entry.stride = sizeof(ResourceBinding);
				});

			for_each_bit(set_layout.storage_image_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				if (set_layout.fp_mask & (1u << binding))
					entry.offset = offsetof(ResourceBinding, image.fp) + sizeof(ResourceBinding) * binding;
				else
					entry.offset = offsetof(ResourceBinding, image.integer) + sizeof(ResourceBinding) * binding;
				entry.stride = sizeof(ResourceBinding);
				});

			for_each_bit(set_layout.input_attachment_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				VK_ASSERT(update_count < VULKAN_NUM_BINDINGS);
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				if (set_layout.fp_mask & (1u << binding))
					entry.offset = offsetof(ResourceBinding, image.fp) + sizeof(ResourceBinding) * binding;
				else
					entry.offset = offsetof(ResourceBinding, image.integer) + sizeof(ResourceBinding) * binding;
				entry.stride = sizeof(ResourceBinding);
				});

			VkDescriptorUpdateTemplateCreateInfoKHR info = { VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO_KHR };
			info.pipelineLayout = pipe_layout;
			info.descriptorSetLayout = set_allocators[desc_set]->GetLayout();
			info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET_KHR;
			info.set = desc_set;
			info.descriptorUpdateEntryCount = update_count;
			info.pDescriptorUpdateEntries = update_entries;
			info.pipelineBindPoint = (layout.stages_for_sets[desc_set] & VK_SHADER_STAGE_COMPUTE_BIT) ?
				VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

			if (table.vkCreateDescriptorUpdateTemplateKHR(device->GetDevice(), &info, nullptr,
				&update_template[desc_set]) != VK_SUCCESS)
			{
				QM_LOG_ERROR("Failed to create descriptor update template.\n");
			}
		}
	}

	PipelineLayout::~PipelineLayout()
	{
		auto& table = device->GetDeviceTable();
		if (pipe_layout != VK_NULL_HANDLE)
			table.vkDestroyPipelineLayout(device->GetDevice(), pipe_layout, nullptr);

		for (auto& update : update_template)
			if (update != VK_NULL_HANDLE)
				table.vkDestroyDescriptorUpdateTemplateKHR(device->GetDevice(), update, nullptr);
	}

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
				if (type.array.front() == 0)
				{
					// Runtime array.
					if (!device->GetDeviceFeatures().supports_descriptor_indexing)
						QM_LOG_ERROR("Sufficient features for descriptor indexing is not supported on this device.\n");

					if (binding != 0)
						QM_LOG_ERROR("Bindless textures can only be used with binding = 0 in a set.\n");

					if (type.basetype != SPIRType::Image || type.image.dim == spv::DimBuffer)
						QM_LOG_ERROR("Can only use bindless for sampled images.\n");
					else
						layout.bindless_set_mask |= 1u << set;

					size = DescriptorSetLayout::UNSIZED_ARRAY;
				}
				else if (size && size != type.array.front())
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

	Shader::Shader(Hash hash, Device* device_, const uint32_t* data, size_t size)
		: IntrusiveHashMapEnabled<Shader>(hash)
		, device(device_)
	{
		VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		info.codeSize = size;
		info.pCode = data;

#ifdef VULKAN_DEBUG
		QM_LOG_ERROR("Creating shader module.\n");
#endif
		auto& table = device->GetDeviceTable();
		if (table.vkCreateShaderModule(device->GetDevice(), &info, nullptr, &module) != VK_SUCCESS)
			QM_LOG_ERROR("Failed to create shader module.\n");

		device->register_shader_module(module, get_hash(), info);

		Compiler compiler(data, size / sizeof(uint32_t));

		auto resources = compiler.get_shader_resources();
		for (auto& image : resources.sampled_images)
		{
			auto set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
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
			auto binding = compiler.get_decoration(buffer.id, spv::DecorationBinding);
			layout.sets[set].uniform_buffer_mask |= 1u << binding;
			UpdateArrayInfo(compiler.get_type(buffer.type_id), set, binding);
		}

		for (auto& buffer : resources.storage_buffers)
		{
			auto set = compiler.get_decoration(buffer.id, spv::DecorationDescriptorSet);
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
			layout.push_constant_size =
				compiler.get_declared_struct_size(compiler.get_type(resources.push_constant_buffers.front().base_type_id));
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
		if (module)
			table.vkDestroyShaderModule(device->GetDevice(), module, nullptr);
	}

	void Program::SetShader(ShaderStage stage, Shader* handle)
	{
		shaders[Util::ecast(stage)] = handle;
	}

	Program::Program(Device* device_, Shader* vertex, Shader* fragment)
		: device(device_)
	{
		SetShader(ShaderStage::Vertex, vertex);
		SetShader(ShaderStage::Fragment, fragment);
		device->BakeProgram(*this);
	}

	Program::Program(Device* device_, Shader* compute_shader)
		: device(device_)
	{
		SetShader(ShaderStage::Compute, compute_shader);
		device->BakeProgram(*this);
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

	Program::~Program()
	{
		for (auto& pipe : pipelines)
		{
			if (internal_sync)
				device->DestroyPipelineNolock(pipe.get());
			else
				device->DestroyPipeline(pipe.get());
		}
	}
}
