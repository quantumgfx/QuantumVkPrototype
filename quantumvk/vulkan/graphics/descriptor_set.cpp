#include "descriptor_set.hpp"
#include "quantumvk/vulkan/device.hpp"

#include <vector>

using namespace Util;

namespace Vulkan
{
	
	///////////////////////////////////////////////////////////////
	//Static inline helper functions///////////////////////////////
	///////////////////////////////////////////////////////////////

	static inline void SetDescriptorSetMask(Program& program, uint32_t& descriptor_set_mask)
	{
		descriptor_set_mask = 0;
		// Retrives the largest set that is used by any shader
		for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
		{
			ShaderStage shader_type = static_cast<ShaderStage>(i);
			if (!program.HasShader(shader_type))
				continue;

			descriptor_set_mask |= program.GetShader(shader_type)->GetLayout().set_mask;
		}
	}

	static inline void FillDescriptorSetArraySizes(Program& program, uint32_t descriptor_set_mask, uint32_t desc_set_array_sizes[][VULKAN_NUM_BINDINGS])
	{
		// Fills array info
		for (unsigned i = 0; i < static_cast<unsigned>(ShaderStage::Count); i++)
		{
			ShaderStage shader_type = static_cast<ShaderStage>(i);
			if (!program.HasShader(shader_type))
				continue;

			const auto& shader_layout = program.GetShader(shader_type)->GetLayout();
			for (uint32_t set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++) // For every set.
			{
				if ((descriptor_set_mask & (1u << set)) && (shader_layout.set_mask & (1u << set)))// If set active
				{
					for (uint32_t binding = 0; binding < VULKAN_NUM_BINDINGS; binding++) // For every binding within that set
					{
						uint32_t array_size = shader_layout.sets[set].array_size[binding];
						if (array_size != 0) // If binding is active 
						{
#ifdef VULKAN_DEBUG
							if (desc_set_array_sizes[set][binding] != 0 && desc_set_array_sizes[set][binding] != array_size) // If array size is set and doesn't match
								QM_LOG_ERROR("Mismatch between array sizes in different shaders at (set: %u, binding: %u). One array size is %u and the other is %u.\n", set, binding, desc_set_array_sizes[set][binding], array_size); // Error
							else // Else
								desc_set_array_sizes[set][binding] = array_size; // Set array size
#else
							desc_set_array_sizes[set][binding] = array_size;
#endif
						}
					}

				}
			}
		}
	}

	static inline void FillPerSetStagesAndLayout(Shader& shader, VkShaderStageFlags stage_flags, std::vector<UniformManager::PerSet>& sets)
	{

		const auto& shader_layout = shader.GetLayout();

		for (uint32_t set = 0; set < sets.size(); set++)
		{
			if (shader_layout.set_mask & (1 << set))
			{
				auto& desc_set = sets[set];

				desc_set.stages |= stage_flags;

				desc_set.layout.sampled_image_mask |= shader_layout.sets[set].sampled_image_mask;
				desc_set.layout.storage_image_mask |= shader_layout.sets[set].storage_image_mask;
				desc_set.layout.uniform_buffer_mask |= shader_layout.sets[set].uniform_buffer_mask;
				desc_set.layout.storage_buffer_mask |= shader_layout.sets[set].storage_buffer_mask;
				desc_set.layout.sampled_buffer_mask |= shader_layout.sets[set].sampled_buffer_mask;
				desc_set.layout.input_attachment_mask |= shader_layout.sets[set].input_attachment_mask;
				desc_set.layout.sampler_mask |= shader_layout.sets[set].sampler_mask;
				desc_set.layout.separate_image_mask |= shader_layout.sets[set].separate_image_mask;
				desc_set.layout.fp_mask |= shader_layout.sets[set].fp_mask;

				Util::ForEachBit(shader_layout.sets[set].immutable_sampler_mask, [&](uint32_t binding) {
					StockSampler sampler = GetImmutableSampler(shader_layout.sets[set], binding);

					// Do we already have an immutable sampler? Make sure it matches the layout.
					if (HasImmutableSampler(desc_set.layout, binding))
					{
						if (sampler != GetImmutableSampler(desc_set.layout, binding))
							QM_LOG_ERROR("Immutable sampler mismatch detected!\n");
					}

					SetImmutableSampler(desc_set.layout, binding, sampler);
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

				Util::ForEachBit(active_binds, [&](uint32_t binding)
					{
						desc_set.binding_stages[binding] |= stage_flags;

						auto& combined_size = desc_set.layout.array_size[binding];
						auto& shader_size = shader_layout.sets[set].array_size[binding];
						if (desc_set.layout.array_size[binding] && desc_set.layout.array_size[binding] != shader_layout.sets[set].array_size[binding])
						{
							QM_LOG_ERROR("Mismatch between array sizes in different shaders.\n");
							VK_ASSERT(false);
						}
						else
							desc_set.layout.array_size[binding] = shader_layout.sets[set].array_size[binding];
					});
			}
		}
	}

	static inline void FillPushConstantRange(Shader& shader, VkShaderStageFlags stage_flags, VkPushConstantRange& push_constant_range)
	{
		const auto& shader_layout = shader.GetLayout();

		// Merge push constant ranges into one range.
		// Do not try to split into multiple ranges as it just complicates things for no obvious gain.
		if (shader_layout.push_constant_size != 0)
		{
			push_constant_range.stageFlags |= stage_flags;
			push_constant_range.size = std::max(push_constant_range.size, shader_layout.push_constant_size);
		}
	}

	static inline void FillPerSetPoolSizesAndVkLayouts(Device* device, uint32_t descriptor_set_mask, std::vector<UniformManager::PerSet>& sets)
	{
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		for (uint32_t set = 0; set < sets.size(); set++)
		{
			if (descriptor_set_mask & (1u << set))
			{
				auto& desc_set = sets[set];
				auto& pool_size = desc_set.pool_size;

				VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
				VkDescriptorSetLayoutBindingFlagsCreateInfoEXT flags = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
				VkDescriptorBindingFlagsEXT binding_flags = 0;

				for (unsigned binding = 0; binding < VULKAN_NUM_BINDINGS; binding++)
				{
					auto stages = desc_set.binding_stages[binding];
					if (stages == 0)
						continue;

					uint32_t array_size = desc_set.layout.array_size[binding];
					uint32_t pool_array_size = array_size * VULKAN_NUM_SETS_PER_POOL;

					uint32_t types = 0;
					if (desc_set.layout.sampled_image_mask & (1u << binding))
					{
						VkSampler sampler = VK_NULL_HANDLE;
						if (HasImmutableSampler(desc_set.layout, binding))
							sampler = device->GetStockSampler(GetImmutableSampler(desc_set.layout, binding)).GetSampler();

						bindings.push_back({ binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, array_size, stages, sampler != VK_NULL_HANDLE ? &sampler : nullptr });
						pool_size.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, pool_array_size });
						types++;
					}

					if (desc_set.layout.sampled_buffer_mask & (1u << binding))
					{
						bindings.push_back({ binding, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, array_size, stages, nullptr });
						pool_size.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, pool_array_size });
						types++;
					}

					if (desc_set.layout.storage_image_mask & (1u << binding))
					{
						bindings.push_back({ binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, array_size, stages, nullptr });
						pool_size.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, pool_array_size });
						types++;
					}

					if (desc_set.layout.uniform_buffer_mask & (1u << binding))
					{
						bindings.push_back({ binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, array_size, stages, nullptr });
						pool_size.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, pool_array_size });
						types++;
					}

					if (desc_set.layout.storage_buffer_mask & (1u << binding))
					{
						bindings.push_back({ binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, array_size, stages, nullptr });
						pool_size.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, pool_array_size });
						types++;
					}

					if (desc_set.layout.input_attachment_mask & (1u << binding))
					{
						bindings.push_back({ binding, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, array_size, stages, nullptr });
						pool_size.push_back({ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, pool_array_size });
						types++;
					}

					if (desc_set.layout.separate_image_mask & (1u << binding))
					{
						bindings.push_back({ binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, array_size, stages, nullptr });
						pool_size.push_back({ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, pool_array_size });
						types++;
					}

					if (desc_set.layout.sampler_mask & (1u << binding))
					{
						VkSampler sampler = VK_NULL_HANDLE;
						if (HasImmutableSampler(desc_set.layout, binding))
							sampler = device->GetStockSampler(GetImmutableSampler(desc_set.layout, binding)).GetSampler();

						bindings.push_back({ binding, VK_DESCRIPTOR_TYPE_SAMPLER, array_size, stages, sampler != VK_NULL_HANDLE ? &sampler : nullptr });
						pool_size.push_back({ VK_DESCRIPTOR_TYPE_SAMPLER, pool_array_size });
						types++;
					}
				}

				if (!bindings.empty())
				{
					info.bindingCount = bindings.size();
					info.pBindings = bindings.data();
				}

#ifdef VULKAN_DEBUG
				QM_LOG_INFO("Creating descriptor set layout.\n");
#endif
				if (device->GetDeviceTable().vkCreateDescriptorSetLayout(device->GetDevice(), &info, nullptr, &desc_set.vk_set_layout) != VK_SUCCESS)
					QM_LOG_ERROR("Failed to create descriptor set layout.");

				bindings.clear();
			}
		}
	}

	static inline void CreateUniformLayout(Device* device, uint32_t descriptor_set_mask, std::vector<UniformManager::PerSet>& sets, VkPushConstantRange& push_constant_range, VkPipelineLayout& uniform_layout)
	{
		VkDescriptorSetLayout layouts[VULKAN_NUM_DESCRIPTOR_SETS] = {};

		for (uint32_t i = 0;i < sets.size();i++)
		{
			if (descriptor_set_mask & (1 << i))
				layouts[i] = sets[i].vk_set_layout;
			else
				layouts[i] = VK_NULL_HANDLE;
		}

		if (sets.size() > device->GetGPUProperties().limits.maxBoundDescriptorSets)
		{
			QM_LOG_ERROR("Number of sets %zu exceeds device limit of %u.\n", sets.size(), device->GetGPUProperties().limits.maxBoundDescriptorSets);
		}

		VkPipelineLayoutCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		if (sets.size())
		{
			info.setLayoutCount = sets.size();
			info.pSetLayouts = layouts;
		}

		if (push_constant_range.stageFlags != 0)
		{
			info.pushConstantRangeCount = 1;
			info.pPushConstantRanges = &push_constant_range;
		}

#ifdef VULKAN_DEBUG
		QM_LOG_INFO("Creating uniform layout.\n");
#endif
		auto& table = device->GetDeviceTable();
		if (table.vkCreatePipelineLayout(device->GetDevice(), &info, nullptr, &uniform_layout) != VK_SUCCESS)
			QM_LOG_ERROR("Failed to create uniform layout.\n");
	}

	static inline void CreateUpdateTemplates(Device* device, VkPipelineLayout uniform_layout, uint32_t descriptor_set_mask, std::vector<UniformManager::PerSet>& sets, uint32_t resource_count, uint32_t resource_offsets[][VULKAN_NUM_BINDINGS])
	{
		VkDescriptorUpdateTemplateEntryKHR* update_entries = new VkDescriptorUpdateTemplateEntryKHR[resource_count];

		auto& table = device->GetDeviceTable();
		for (uint32_t desc_set = 0; desc_set < sets.size(); desc_set++)
		{
			if ((descriptor_set_mask & (1u << desc_set)) == 0)
				continue;

			uint32_t update_count = 0;

			auto& set_layout = sets[desc_set].layout;

			ForEachBit(set_layout.uniform_buffer_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = sizeof(ResourceBinding) * resource_offsets[desc_set][binding] + offsetof(ResourceBinding, buffer);
				entry.stride = sizeof(ResourceBinding);
				});

			ForEachBit(set_layout.storage_buffer_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = sizeof(ResourceBinding) * resource_offsets[desc_set][binding] + offsetof(ResourceBinding, buffer);
				entry.stride = sizeof(ResourceBinding);
				});

			ForEachBit(set_layout.sampled_buffer_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = sizeof(ResourceBinding) * resource_offsets[desc_set][binding] + offsetof(ResourceBinding, buffer_view);
				entry.stride = sizeof(ResourceBinding);
				});


			ForEachBit(set_layout.sampled_image_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = sizeof(ResourceBinding) * resource_offsets[desc_set][binding] + offsetof(ResourceBinding, image);
				//entry.offset = offsetof(UniformBinding, resource) + offsetof(ResourceBinding, image) + sizeof(ResourceBinding) * offsets[binding];
				entry.stride = sizeof(ResourceBinding);
				});

			ForEachBit(set_layout.separate_image_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = sizeof(ResourceBinding) * resource_offsets[desc_set][binding] + offsetof(ResourceBinding, image);
				//entry.offset = offsetof(UniformBinding, resource) + offsetof(ResourceBinding, image) + sizeof(ResourceBinding) * offsets[binding];
				entry.stride = sizeof(ResourceBinding);
				});

			ForEachBit(set_layout.sampler_mask & ~set_layout.immutable_sampler_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = sizeof(ResourceBinding) * resource_offsets[desc_set][binding] + offsetof(ResourceBinding, image);
				//entry.offset = offsetof(UniformBinding, resource) + offsetof(ResourceBinding, image) + sizeof(ResourceBinding) * offsets[binding];
				entry.stride = sizeof(ResourceBinding);
				});

			ForEachBit(set_layout.storage_image_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = sizeof(ResourceBinding) * resource_offsets[desc_set][binding] + offsetof(ResourceBinding, image);
				//entry.offset = offsetof(UniformBinding, resource) + offsetof(ResourceBinding, image) + sizeof(ResourceBinding) * offsets[binding];
				entry.stride = sizeof(ResourceBinding);
				});

			ForEachBit(set_layout.input_attachment_mask, [&](uint32_t binding) {
				unsigned array_size = set_layout.array_size[binding];
				auto& entry = update_entries[update_count++];
				entry.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
				entry.dstBinding = binding;
				entry.dstArrayElement = 0;
				entry.descriptorCount = array_size;
				entry.offset = sizeof(ResourceBinding) * resource_offsets[desc_set][binding] + offsetof(ResourceBinding, image);
				//entry.offset = offsetof(UniformBinding, resource) + offsetof(ResourceBinding, image) + sizeof(ResourceBinding) * offsets[binding];
				entry.stride = sizeof(ResourceBinding);
				});

			VkDescriptorUpdateTemplateCreateInfoKHR info = { VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO_KHR };
			info.pipelineLayout = uniform_layout;
			info.descriptorSetLayout = sets[desc_set].vk_set_layout;
			info.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET_KHR;
			info.set = desc_set;
			info.descriptorUpdateEntryCount = update_count;
			info.pDescriptorUpdateEntries = update_entries;
			info.pipelineBindPoint = (sets[desc_set].stages & VK_SHADER_STAGE_COMPUTE_BIT) ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

			if (table.vkCreateDescriptorUpdateTemplateKHR(device->GetDevice(), &info, nullptr, &sets[desc_set].update_template) != VK_SUCCESS)
			{
				QM_LOG_ERROR("Failed to create descriptor update template.\n");
			}

		}

		delete[] update_entries;
	}

	///////////////////////////////////////////////////////////////
	//Actual class implementation//////////////////////////////////
	///////////////////////////////////////////////////////////////

	void UniformManager::InitUniforms(Device* device_, Program& program)
	{
		device = device_;
		// -----------FILL SET MASK AND COUNT--------------

		SetDescriptorSetMask(program, descriptor_set_mask);

		if (!descriptor_set_mask)
			descriptor_set_count = 0;
		else
			descriptor_set_count = Util::GetMostSignificantBitSet(descriptor_set_mask) + 1;

		// -----------------------------------------------
		// --------FILL RESOURCE OFFSETS AND COUNT--------

		uint32_t desc_set_array_sizes[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};

		FillDescriptorSetArraySizes(program, descriptor_set_mask, desc_set_array_sizes);

		resource_count = 0;
		for (uint32_t set = 0; set < VULKAN_NUM_DESCRIPTOR_SETS; set++) // For every set.
		{
			for (uint32_t binding = 0; binding < VULKAN_NUM_BINDINGS; binding++) // For every binding within that set
			{
				resource_offsets[set][binding] = resource_count;
				resource_count += desc_set_array_sizes[set][binding];
			}
		}

		// --------------------------------------------------
		// ------------FILL PER SET INFO---------------------

		sets.resize(descriptor_set_count);

		for (uint32_t i = 0; i < static_cast<uint32_t>(ShaderStage::Count); i++)
		{
			ShaderStage shader_type = static_cast<ShaderStage>(i);
			if (!program.HasShader(shader_type))
				continue;

			VkShaderStageFlags stage_flags = Shader::StageToVkType(shader_type);
			auto& shader = program.GetShader(shader_type);

			FillPerSetStagesAndLayout(*shader, stage_flags, sets);
			FillPushConstantRange(*shader, stage_flags, push_constant_range);
		}

		FillPerSetPoolSizesAndVkLayouts(device, descriptor_set_mask, sets);

		CreateUniformLayout(device, descriptor_set_mask, sets, push_constant_range, uniform_layout);

		if (device->GetDeviceExtensions().supports_update_template)
			CreateUpdateTemplates(device, uniform_layout, descriptor_set_mask, sets, resource_count, resource_offsets);

		// --------------------------------------------------

		threads.resize(device->num_thread_indices);

		// TODO find some alternative to this disgustingly large number of new allocations

		for (auto& set : sets)
		{
			for (uint32_t i = 0; i < device->num_thread_indices; i++)
				set.threads.emplace_back(new PerThreadPerSet);
		}
			
	}

	UniformManager::UniformManager()
	{
	}

	UniformManager::~UniformManager()
	{
		VK_ASSERT(device);

		auto& table = device->GetDeviceTable();
		if (uniform_layout != VK_NULL_HANDLE)
			table.vkDestroyPipelineLayout(device->GetDevice(), uniform_layout, nullptr);

		Clear();

		for (uint32_t set = 0; set < descriptor_set_count; set++)
		{
			if (descriptor_set_mask & (1 << set))
			{
				if (sets[set].update_template != VK_NULL_HANDLE)
					table.vkDestroyDescriptorUpdateTemplateKHR(device->GetDevice(), sets[set].update_template, nullptr);

				if (sets[set].vk_set_layout != VK_NULL_HANDLE)
					table.vkDestroyDescriptorSetLayout(device->GetDevice(), sets[set].vk_set_layout, nullptr);
			}
		}
	}


	ResourceBinding& UniformManager::GetUniformResource(uint32_t thread_index, uint32_t set, uint32_t binding, uint32_t array_index)
	{
		CheckForNewThread(thread_index);
		return *(threads[thread_index].manager.GetResourceArray() + resource_offsets[set][binding] + array_index);
	}

	void UniformManager::SetUniformResource(uint32_t thread_index, uint32_t set, uint32_t binding, uint32_t array_index, const ResourceBinding& resource)
	{
		CheckForNewThread(thread_index);
		*(threads[thread_index].manager.GetResourceArray() + resource_offsets[set][binding] + array_index) = resource;
	}

	VkDescriptorSet UniformManager::FlushDescriptorSet(uint32_t thread_index, uint32_t set)
	{
		VK_ASSERT(device);

		if ((descriptor_set_mask & (1 << set)) == 0)
			return VK_NULL_HANDLE;

		CheckForNewThread(thread_index);

		auto desc_set = FindDescriptorSet(thread_index, set);

		// If hash differs, update the resource
		if (desc_set.needs_update)
		{
			auto update_template = sets[set].update_template;

			if (update_template != VK_NULL_HANDLE) // If Update templates exist, use them as they are both faster and easier to use.
				device->GetDeviceTable().vkUpdateDescriptorSetWithTemplateKHR(device->GetDevice(), desc_set.vk_set, update_template, threads[thread_index].manager.GetResourceArray());
			else // Update with standard descriptor writes.
				UpdateDescriptorSetLegacy(thread_index, set, desc_set.vk_set);
		}

		return desc_set.vk_set;
	}

	void UniformManager::BeginFrame()
	{
		for (uint32_t set = 0; set < descriptor_set_count; set++)
			if (descriptor_set_mask & (1 << set))
				for (auto& thr : sets[set].threads)
					thr->should_begin = true;
	}

	void UniformManager::Clear()
	{
		for (uint32_t set = 0; set < descriptor_set_count; set++)
		{
			if (descriptor_set_mask & (1 << set))
			{
				for (auto& thr : sets[set].threads)
				{
					thr->set_nodes.clear();
					for (auto& pool : thr->pools)
					{
						device->GetDeviceTable().vkResetDescriptorPool(device->GetDevice(), pool, 0);
						device->GetDeviceTable().vkDestroyDescriptorPool(device->GetDevice(), pool, nullptr);
					}
					thr->pools.clear();
				}
			}
		}
	}

	void UniformManager::CheckForNewThread(uint32_t thread_index)
	{
		VK_ASSERT(thread_index < device->num_thread_indices);

		if (!threads[thread_index].active)
		{
			threads[thread_index].manager.CreateResourceArray(resource_count);
			threads[thread_index].active = true;
		}
	}

	UniformManager::HashedDescriptorSet UniformManager::FindDescriptorSet(uint32_t thread_index, uint32_t set)
	{
		auto& set_layout = sets[set].layout;

		// Hash descriptor set info
		Util::Hasher h;

		h.u32(set_layout.fp_mask);

		ResourceBinding* resource_array = threads[thread_index].manager.GetResourceArray();

		// UBOs
		Util::ForEachBit(set_layout.uniform_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				const auto& b = *(resource_array + resource_offsets[set][binding] + i);
				h.u64(b.cookie);
				h.u32(b.buffer.range);
				VK_ASSERT(b.buffer.buffer != VK_NULL_HANDLE);
			}
			});

		// SSBOs
		Util::ForEachBit(set_layout.storage_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				const auto& b = *(resource_array + resource_offsets[set][binding] + i);
				h.u64(b.cookie);
				h.u32(b.buffer.offset);
				h.u32(b.buffer.range);
				VK_ASSERT(b.buffer.buffer != VK_NULL_HANDLE);
			}
			});

		// Sampled buffers
		Util::ForEachBit(set_layout.sampled_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				const auto& b = *(resource_array + resource_offsets[set][binding] + i);
				h.u64(b.cookie);
				VK_ASSERT(b.buffer_view != VK_NULL_HANDLE);
			}
			});

		// Sampled images
		Util::ForEachBit(set_layout.sampled_image_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				const auto& b = *(resource_array + resource_offsets[set][binding] + i);
				h.u64(b.cookie);
				if (!HasImmutableSampler(set_layout, binding + i))
				{
					h.u64(b.secondary_cookie);
					//VK_ASSERT(b.resource.image.fp.sampler != VK_NULL_HANDLE);
					VK_ASSERT(b.image.sampler != VK_NULL_HANDLE);
				}
				h.u32(b.image.imageLayout);
				VK_ASSERT(b.image.imageView != VK_NULL_HANDLE);
			}
			});

		// Separate images
		Util::ForEachBit(set_layout.separate_image_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				const auto& b = *(resource_array + resource_offsets[set][binding] + i);
				h.u64(b.cookie);
				h.u32(b.image.imageLayout);
				VK_ASSERT(b.image.imageView != VK_NULL_HANDLE);
			}
			});

		// Separate samplers
		Util::ForEachBit(set_layout.sampler_mask & ~set_layout.immutable_sampler_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				const auto& b = *(resource_array + resource_offsets[set][binding] + i);
				h.u64(b.cookie);
				VK_ASSERT(b.image.sampler != VK_NULL_HANDLE);
			}
			});

		// Storage images
		Util::ForEachBit(set_layout.storage_image_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				const auto& b = *(resource_array + resource_offsets[set][binding] + i);
				h.u64(b.cookie);
				h.u32(b.image.imageLayout);
				VK_ASSERT(b.image.imageView != VK_NULL_HANDLE);
			}
			});

		// Input attachments
		Util::ForEachBit(set_layout.input_attachment_mask, [&](uint32_t binding) {
			unsigned array_size = set_layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				const auto& b = *(resource_array + resource_offsets[set][binding] + i);
				h.u64(b.cookie);
				h.u32(b.image.imageLayout);
				VK_ASSERT(b.image.imageView != VK_NULL_HANDLE);
			}
			});

		Util::Hash hash = h.get();

		auto& state = sets[set].threads[thread_index];
		if (state->should_begin)
		{
			state->set_nodes.begin_frame();
			state->should_begin = false;
		}

		HashedDescriptorSet hashed_set{};

		auto* node = state->set_nodes.request(hash);
		if (node)
		{
			hashed_set.vk_set = node->set;
			hashed_set.needs_update = false;
			return hashed_set;
		}

		node = state->set_nodes.request_vacant(hash);
		if (node)
		{
			hashed_set.vk_set = node->set;
			hashed_set.needs_update = true;
			return hashed_set;
		}

		VkDescriptorPool pool;
		VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		info.maxSets = VULKAN_NUM_SETS_PER_POOL;
		if (!sets[set].pool_size.empty())
		{
			info.poolSizeCount = sets[set].pool_size.size();
			info.pPoolSizes = sets[set].pool_size.data();
		}

		if (device->GetDeviceTable().vkCreateDescriptorPool(device->GetDevice(), &info, nullptr, &pool) != VK_SUCCESS)
		{
			QM_LOG_ERROR("Failed to create descriptor pool.\n");
			hashed_set.vk_set = VK_NULL_HANDLE;
			hashed_set.needs_update = true;
			return hashed_set;
		}

		VkDescriptorSet desc_sets[VULKAN_NUM_SETS_PER_POOL];
		VkDescriptorSetLayout layouts[VULKAN_NUM_SETS_PER_POOL];
		std::fill(std::begin(layouts), std::end(layouts), sets[set].vk_set_layout);

		VkDescriptorSetAllocateInfo alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		alloc.descriptorPool = pool;
		alloc.descriptorSetCount = VULKAN_NUM_SETS_PER_POOL;
		alloc.pSetLayouts = layouts;

		if (device->GetDeviceTable().vkAllocateDescriptorSets(device->GetDevice(), &alloc, desc_sets) != VK_SUCCESS)
			QM_LOG_ERROR("Failed to allocate descriptor sets.\n");
		state->pools.push_back(pool);

		for (auto set : desc_sets)
			state->set_nodes.make_vacant(set);

		hashed_set.vk_set = state->set_nodes.request_vacant(hash)->set;
		hashed_set.needs_update = true;
		return hashed_set;
	}

	void UniformManager::UpdateDescriptorSetLegacy(uint32_t thread_index, uint32_t set, VkDescriptorSet desc_set)
	{
		auto& table = device->GetDeviceTable();

		const DescriptorSetLayout& layout = sets[set].layout;

		Util::RetainedDynamicArray<VkWriteDescriptorSet> legacy_set_writes = device->AllocateHeapArray<VkWriteDescriptorSet>(resource_count);

		uint32_t num_bindings = 0;

		ResourceBinding* resource_array = threads[thread_index].manager.GetResourceArray();

		Util::ForEachBit(layout.uniform_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = layout.array_size[binding];
			for (uint32_t i = 0; i < array_size; i++)
			{
				auto& write = legacy_set_writes[num_bindings++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = nullptr;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				write.dstArrayElement = i;
				write.dstBinding = binding;
				write.dstSet = desc_set;
				write.pBufferInfo = &(*(resource_array + resource_offsets[set][binding] + i)).buffer;
			}
			});

		Util::ForEachBit(layout.storage_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				auto& write = legacy_set_writes[num_bindings++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = nullptr;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				write.dstArrayElement = i;
				write.dstBinding = binding;
				write.dstSet = desc_set;
				write.pBufferInfo = &(*(resource_array + resource_offsets[set][binding] + i)).buffer;
			}
			});

		Util::ForEachBit(layout.sampled_buffer_mask, [&](uint32_t binding) {
			unsigned array_size = layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				auto& write = legacy_set_writes[num_bindings++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = nullptr;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
				write.dstArrayElement = i;
				write.dstBinding = binding;
				write.dstSet = desc_set;
				write.pTexelBufferView = &(*(resource_array + resource_offsets[set][binding] + i)).buffer_view;
			}
			});

		Util::ForEachBit(layout.sampled_image_mask, [&](uint32_t binding) {
			unsigned array_size = layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				auto& write = legacy_set_writes[num_bindings++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = nullptr;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				write.dstArrayElement = i;
				write.dstBinding = binding;
				write.dstSet = desc_set;
				write.pImageInfo = &(*(resource_array + resource_offsets[set][binding] + i)).image;

				//write.pImageInfo = &GetDescriptor(set, binding, i).resource.image;
			}
			});

		Util::ForEachBit(layout.separate_image_mask, [&](uint32_t binding) {
			unsigned array_size = layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				auto& write = legacy_set_writes[num_bindings++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = nullptr;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				write.dstArrayElement = i;
				write.dstBinding = binding;
				write.dstSet = desc_set;
				write.pImageInfo = &(*(resource_array + resource_offsets[set][binding] + i)).image;

				//write.pImageInfo = &GetDescriptor(set, binding, i).resource.image;
			}
			});

		Util::ForEachBit(layout.sampler_mask & ~layout.immutable_sampler_mask, [&](uint32_t binding) {
			unsigned array_size = layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				auto& write = legacy_set_writes[num_bindings++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = nullptr;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				write.dstArrayElement = i;
				write.dstBinding = binding;
				write.dstSet = desc_set;
				write.pImageInfo = &(*(resource_array + resource_offsets[set][binding] + i)).image;
			}
			});

		Util::ForEachBit(layout.storage_image_mask, [&](uint32_t binding) {
			unsigned array_size = layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				auto& write = legacy_set_writes[num_bindings++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = nullptr;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				write.dstArrayElement = i;
				write.dstBinding = binding;
				write.dstSet = desc_set;
				write.pImageInfo = &(*(resource_array + resource_offsets[set][binding] + i)).image;

				//write.pImageInfo = &GetDescriptor(set, binding, i).resource.image;
			}
			});

		Util::ForEachBit(layout.input_attachment_mask, [&](uint32_t binding) {
			unsigned array_size = layout.array_size[binding];
			for (unsigned i = 0; i < array_size; i++)
			{
				auto& write = legacy_set_writes[num_bindings++];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.pNext = nullptr;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
				write.dstArrayElement = i;
				write.dstBinding = binding;
				write.dstSet = desc_set;
				write.pImageInfo = &(*(resource_array + resource_offsets[set][binding] + i)).image;

				//write.pImageInfo = &GetDescriptor(set, binding, i).resource.image;
			}
			});

		table.vkUpdateDescriptorSets(device->GetDevice(), num_bindings, legacy_set_writes.Data(), 0, nullptr);

		device->FreeHeapArray(legacy_set_writes);
	}
}
