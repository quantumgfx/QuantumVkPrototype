#include "shader.hpp"
#include "vulkan/device.hpp"
#include "spirv_cross.hpp"

using namespace std;
using namespace spirv_cross;
using namespace Util;

namespace Vulkan
{
	///////////////////////////////
	//Pipeline Layout//////////////
	///////////////////////////////

	PipelineLayout::PipelineLayout(Device* device)
		: device(device)
	{
	}

	PipelineLayout::~PipelineLayout()
	{

		if(vklayout != VK_NULL_HANDLE)
			device->DestroyLayout(vklayout);

		for (auto& allocator : set_allocators)
			device->DestroyDescriptorSetAllocator(allocator);

		// This isn't used in command buffers, so it can be deleted immediately
		auto& table = device->GetDeviceTable();
		for (auto& update : update_templates)
			if (update != VK_NULL_HANDLE)
				table.vkDestroyDescriptorUpdateTemplateKHR(device->GetDevice(), update, nullptr);
	}

	void PipelineLayout::CreateLayout(Program& program)
	{
		if (program.HasShader(ShaderStage::Vertex))
			attribute_mask = program.GetShader(ShaderStage::Vertex)->GetLayout().input_mask;
		if (program.HasShader(ShaderStage::Fragment))
			render_target_mask = program.GetShader(ShaderStage::Fragment)->GetLayout().output_mask;

		descriptor_set_mask = 0;

		for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
		{
			ShaderStage shader_type = static_cast<ShaderStage>(i);
			if (!program.HasShader(shader_type))
				continue;

			auto shader = program.GetShader(static_cast<ShaderStage>(i));

			uint32_t stage_mask = 1u << i;

			auto& shader_layout = shader->GetLayout();
			for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
			{
				sets[set].sampled_image_mask |= shader_layout.sets[set].sampled_image_mask;
				sets[set].storage_image_mask |= shader_layout.sets[set].storage_image_mask;
				sets[set].uniform_buffer_mask |= shader_layout.sets[set].uniform_buffer_mask;
				sets[set].storage_buffer_mask |= shader_layout.sets[set].storage_buffer_mask;
				sets[set].sampled_buffer_mask |= shader_layout.sets[set].sampled_buffer_mask;
				sets[set].input_attachment_mask |= shader_layout.sets[set].input_attachment_mask;
				sets[set].sampler_mask |= shader_layout.sets[set].sampler_mask;
				sets[set].separate_image_mask |= shader_layout.sets[set].separate_image_mask;
				sets[set].fp_mask |= shader_layout.sets[set].fp_mask;

				Util::for_each_bit(shader_layout.sets[set].immutable_sampler_mask, [&](uint32_t binding) {
					StockSampler sampler = GetImmutableSampler(shader_layout.sets[set], binding);

					// Do we already have an immutable sampler? Make sure it matches the layout.
					if (HasImmutableSampler(sets[set], binding))
					{
						if (sampler != GetImmutableSampler(sets[set], binding))
							QM_LOG_ERROR("Immutable sampler mismatch detected!\n");
					}

					SetImmutableSampler(sets[set], binding, sampler);
					});

				uint32_t active_binds =
					shader_layout.sets[set].sampled_image_mask |
					shader_layout.sets[set].storage_image_mask |
					shader_layout.sets[set].uniform_buffer_mask |
					shader_layout.sets[set].storage_buffer_mask |
					shader_layout.sets[set].sampled_buffer_mask |
					shader_layout.sets[set].input_attachment_mask |
					shader_layout.sets[set].sampler_mask |
					shader_layout.sets[set].separate_image_mask;

				if (active_binds)
					sets[set].stages |= stage_mask;

				Util::for_each_bit(active_binds, [&](uint32_t bit) {
					sets[set].binding_stages[bit] |= stage_mask;

					auto& combined_size = sets[set].array_size[bit];
					auto& shader_size = shader_layout.sets[set].array_size[bit];
					if (combined_size && combined_size != shader_size)
						QM_LOG_ERROR("Mismatch between array sizes in different shaders.\n");
					else
						combined_size = shader_size;
					});
			}

			// Merge push constant ranges into one range.
			// Do not try to split into multiple ranges as it just complicates things for no obvious gain.
			if (shader_layout.push_constant_size != 0)
			{
				push_constant_range.stageFlags |= 1u << i;
				push_constant_range.size = std::max(push_constant_range.size, shader_layout.push_constant_size);
			}

			spec_constant_mask[i] = shader_layout.spec_constant_mask;
			combined_spec_constant_mask |= shader_layout.spec_constant_mask;
			//bindless_descriptor_set_mask |= shader_layout.bindless_set_mask;
		}

		for (unsigned set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++)
		{
			if (sets[set].stages != 0)
			{
				descriptor_set_mask |= 1u << set;

				for (unsigned binding = 0; binding < VULKAN_NUM_BINDINGS; binding++)
				{
					auto& array_size = sets[set].array_size[binding];
					//if (array_size == DescriptorSetLayout::UNSIZED_ARRAY)
					//{
					//	for (unsigned i = 1; i < VULKAN_NUM_BINDINGS; i++)
					//	{
					//		if (sets[set].binding_stages[i] != 0)
					//			QM_LOG_ERROR("Using bindless for set = %u, but binding = %u has a descriptor attached to it.\n", set, i);
					//	}

					//	// Allows us to have one unified descriptor set layout for bindless.
					//	sets[set].binding_stages[binding] = VK_SHADER_STAGE_ALL;
					//}
					if (array_size == 0)
					{
						array_size = 1;
					}
					else
					{
						for (unsigned i = 1; i < array_size; i++)
						{
							if (sets[set].binding_stages[binding + i] != 0)
							{
								QM_LOG_ERROR("Detected binding aliasing for (%u, %u). Binding array with %u elements starting at (%u, %u) overlaps.\n", set, binding + i, array_size, set, binding);
							}
						}
					}
				}
			}
		}

		VkDescriptorSetLayout layouts[VULKAN_NUM_DESCRIPTOR_SETS] = {};
		unsigned num_sets = 0;
		for (unsigned i = 0; i < VULKAN_NUM_DESCRIPTOR_SETS; i++)
		{
			set_allocators[i] = device->CreateSetAllocator(sets[i]);
			layouts[i] = set_allocators[i]->GetLayout();
			if (descriptor_set_mask & (1u << i))
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

		if (push_constant_range.stageFlags != 0)
		{
			info.pushConstantRangeCount = 1;
			info.pPushConstantRanges = &push_constant_range;
		}

#ifdef VULKAN_DEBUG
		QM_LOG_INFO("Creating pipeline layout.\n");
#endif
		auto& table = device->GetDeviceTable();
		if (table.vkCreatePipelineLayout(device->GetDevice(), &info, nullptr, &vklayout) != VK_SUCCESS)
			QM_LOG_ERROR("Failed to create pipeline layout.\n");

		if (device->GetDeviceFeatures().supports_update_template)
			CreateUpdateTemplates();

		// Compute pipeline layout hash
		Util::Hasher h;
		h.data(reinterpret_cast<const uint32_t*>(sets), sizeof(sets));
		h.u32(push_constant_range.stageFlags);
		h.u32(push_constant_range.size);
		h.data(spec_constant_mask, sizeof(spec_constant_mask));
		h.u32(attribute_mask);
		h.u32(render_target_mask);

		hash = h.get();
	}

	void PipelineLayout::CreateUpdateTemplates()
	{
		auto& table = device->GetDeviceTable();
		for (unsigned desc_set = 0; desc_set < VULKAN_NUM_DESCRIPTOR_SETS; desc_set++)
		{
			if ((descriptor_set_mask & (1u << desc_set)) == 0)
				continue;
			/*if ((bindless_descriptor_set_mask & (1u << desc_set)) == 0)
				continue;*/

			VkDescriptorUpdateTemplateEntryKHR update_entries[VULKAN_NUM_BINDINGS];
			uint32_t update_count = 0;

			auto& set_layout = sets[desc_set];

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
			info.pipelineLayout = vklayout;
			info.descriptorSetLayout = set_allocators[desc_set]->GetLayout();
			info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET_KHR;
			info.set = desc_set;
			info.descriptorUpdateEntryCount = update_count;
			info.pDescriptorUpdateEntries = update_entries;
			info.pipelineBindPoint = (set_layout.stages & VK_SHADER_STAGE_COMPUTE_BIT) ?
				VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

			if (table.vkCreateDescriptorUpdateTemplateKHR(device->GetDevice(), &info, nullptr, &update_templates[desc_set]) != VK_SUCCESS)
			{
				QM_LOG_ERROR("Failed to create descriptor update template.\n");
			}
		}
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
				//	if (!device->GetDeviceFeatures().supports_descriptor_indexing)
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
		if (internal_sync)
			device->DestroyShaderNolock(module);
		else
			device->DestroyShader(module);
	}

	void ShaderDeleter::operator()(Shader* shader)
	{
		shader->device->handle_pool.shaders.free(shader);
	}

	////////////////////////
	//Program///////////////
	////////////////////////

	Program::Program(Device* device_, const GraphicsProgramShaders& graphics_shaders)
		: device(device_), pipeline_layout(device_)
	{
		VK_ASSERT(graphics_shaders.vertex);
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

		shaders = graphics_shaders;
		pipeline_layout.CreateLayout(*this);
	}

	Program::Program(Device* device_, const ComputeProgramShaders& compute_shaders)
		: device(device_), pipeline_layout(device_)
	{
		VK_ASSERT(compute_shaders.compute);
		// Compute program hash
		Util::Hasher hasher;
		hasher.u64(compute_shaders.compute->GetHash());
		hash = hasher.get();
		// --------------------

		shaders = compute_shaders;
		pipeline_layout.CreateLayout(*this);
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
			device->DestroyPipeline(pipe.get());
	}

	void ProgramDeleter::operator()(Program* program)
	{
		program->device->handle_pool.programs.free(program);
	}

	
}
