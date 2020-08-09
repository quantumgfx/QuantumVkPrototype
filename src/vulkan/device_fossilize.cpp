#include "device.hpp"
#include "utils/timer.hpp"

using namespace std;

namespace Vulkan
{

	void Device::register_sampler(VkSampler sampler, Fossilize::Hash hash, const VkSamplerCreateInfo& info)
	{
		state_recorder.record_sampler(sampler, info, hash);
	}

	void Device::register_descriptor_set_layout(VkDescriptorSetLayout layout, Fossilize::Hash hash, const VkDescriptorSetLayoutCreateInfo& info)
	{
		state_recorder.record_descriptor_set_layout(layout, info, hash);
	}

	void Device::register_pipeline_layout(VkPipelineLayout layout, Fossilize::Hash hash, const VkPipelineLayoutCreateInfo& info)
	{
		state_recorder.record_pipeline_layout(layout, info, hash);
	}

	void Device::register_shader_module(VkShaderModule module, Fossilize::Hash hash, const VkShaderModuleCreateInfo& info)
	{
		state_recorder.record_shader_module(module, info, hash);
	}

	void Device::register_compute_pipeline(Fossilize::Hash hash, const VkComputePipelineCreateInfo& info)
	{
		state_recorder.record_compute_pipeline(VK_NULL_HANDLE, info, nullptr, 0, hash);
	}

	void Device::register_graphics_pipeline(Fossilize::Hash hash, const VkGraphicsPipelineCreateInfo& info)
	{
		state_recorder.record_graphics_pipeline(VK_NULL_HANDLE, info, nullptr, 0, hash);
	}

	void Device::register_render_pass(VkRenderPass render_pass, Fossilize::Hash hash, const VkRenderPassCreateInfo& info)
	{
		state_recorder.record_render_pass(render_pass, info, hash);
	}

	bool Device::enqueue_create_shader_module(Fossilize::Hash hash, const VkShaderModuleCreateInfo* create_info, VkShaderModule* module)
	{
		auto* ret = shaders.emplace_yield(hash, hash, this, create_info->pCode, create_info->codeSize);
		*module = ret->GetModule();
		replayer_state.shader_map[*module] = ret;
		return true;
	}

	void Device::notify_replayed_resources_for_type()
	{
#ifdef QM_VULKAN_MT
		if (replayer_state.pipeline_group)
		{
			replayer_state.pipeline_group->wait();
			replayer_state.pipeline_group.Reset();
		}
#endif
	}

	VkPipeline Device::fossilize_create_graphics_pipeline(Fossilize::Hash hash, VkGraphicsPipelineCreateInfo& info)
	{
		if (info.stageCount != 2)
			return VK_NULL_HANDLE;
		if (info.pStages[0].stage != VK_SHADER_STAGE_VERTEX_BIT)
			return VK_NULL_HANDLE;
		if (info.pStages[1].stage != VK_SHADER_STAGE_FRAGMENT_BIT)
			return VK_NULL_HANDLE;

		// Find the Shader* associated with this VkShaderModule and just use that.
		auto vertex_itr = replayer_state.shader_map.find(info.pStages[0].module);
		if (vertex_itr == end(replayer_state.shader_map))
			return VK_NULL_HANDLE;

		// Find the Shader* associated with this VkShaderModule and just use that.
		auto fragment_itr = replayer_state.shader_map.find(info.pStages[1].module);
		if (fragment_itr == end(replayer_state.shader_map))
			return VK_NULL_HANDLE;

		auto* ret = RequestProgram(vertex_itr->second, fragment_itr->second);

		// The layout is dummy, resolve it here.
		info.layout = ret->GetPipelineLayout()->GetLayout();

		register_graphics_pipeline(hash, info);

		QM_LOG_INFO("Creating graphics pipeline.\n");
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkResult res = table->vkCreateGraphicsPipelines(device, pipeline_cache, 1, &info, nullptr, &pipeline);
		if (res != VK_SUCCESS)
			QM_LOG_ERROR("Failed to create graphics pipeline!\n");
		return ret->AddPipeline(hash, pipeline);
	}

	VkPipeline Device::fossilize_create_compute_pipeline(Fossilize::Hash hash, VkComputePipelineCreateInfo& info)
	{
		// Find the Shader* associated with this VkShaderModule and just use that.
		auto itr = replayer_state.shader_map.find(info.stage.module);
		if (itr == end(replayer_state.shader_map))
			return VK_NULL_HANDLE;

		auto* ret = RequestProgram(itr->second);

		// The layout is dummy, resolve it here.
		info.layout = ret->GetPipelineLayout()->GetLayout();

		register_compute_pipeline(hash, info);

		QM_LOG_INFO("Creating compute pipeline.\n");
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkResult res = table->vkCreateComputePipelines(device, pipeline_cache, 1, &info, nullptr, &pipeline);
		if (res != VK_SUCCESS)
			QM_LOG_ERROR("Failed to create compute pipeline!\n");
		return ret->AddPipeline(hash, pipeline);
	}

	bool Device::enqueue_create_graphics_pipeline(Fossilize::Hash hash, const VkGraphicsPipelineCreateInfo* create_info, VkPipeline* pipeline)
	{
#ifdef QM_VULKAN_MT
		if (!replayer_state.pipeline_group)
		{
			std::lock_guard lock(thread_group_mutex);
			replayer_state.pipeline_group = thread_group.create_task();
		}

		replayer_state.pipeline_group->enqueue_task([this, info = *create_info, hash, pipeline]() mutable {
			*pipeline = fossilize_create_graphics_pipeline(hash, info);
		});

		return true;
#else
		auto info = *create_info;
		*pipeline = fossilize_create_graphics_pipeline(hash, info);
		return *pipeline != VK_NULL_HANDLE;
#endif
	}

	bool Device::enqueue_create_compute_pipeline(Fossilize::Hash hash, const VkComputePipelineCreateInfo* create_info, VkPipeline* pipeline)
	{
#ifdef QM_VULKAN_MT
		if (!replayer_state.pipeline_group)
		{
			std::lock_guard lock(thread_group_mutex);
			replayer_state.pipeline_group = thread_group.create_task();
		}

		replayer_state.pipeline_group->enqueue_task([this, info = *create_info, hash, pipeline]() mutable {
			*pipeline = fossilize_create_compute_pipeline(hash, info);
		});

		return true;
#else
		auto info = *create_info;
		*pipeline = fossilize_create_compute_pipeline(hash, info);
		return *pipeline != VK_NULL_HANDLE;
#endif
	}

	bool Device::enqueue_create_render_pass(Fossilize::Hash hash, const VkRenderPassCreateInfo* create_info, VkRenderPass* render_pass)
	{
		auto* ret = render_passes.emplace_yield(hash, hash, this, *create_info);
		*render_pass = ret->GetRenderPass();
		replayer_state.render_pass_map[*render_pass] = ret;
		return true;
	}

	bool Device::enqueue_create_sampler(Fossilize::Hash hash, const VkSamplerCreateInfo*, VkSampler* sampler)
	{
		*sampler = GetStockSampler(static_cast<StockSampler>(hash & 0xffffu)).get_sampler();
		return true;
	}

	bool Device::enqueue_create_descriptor_set_layout(Fossilize::Hash, const VkDescriptorSetLayoutCreateInfo*, VkDescriptorSetLayout* layout)
	{
		// We will create this naturally when building pipelines, can just emit dummy handles.
		*layout = (VkDescriptorSetLayout)uint64_t(-1);
		return true;
	}

	bool Device::enqueue_create_pipeline_layout(Fossilize::Hash, const VkPipelineLayoutCreateInfo*, VkPipelineLayout* layout)
	{
		// We will create this naturally when building pipelines, can just emit dummy handles.
		*layout = (VkPipelineLayout)uint64_t(-1);
		return true;
	}

	bool Device::InitFossilizePipeline(const uint8_t* fossilize_pipeline_data, size_t fossilize_pipeline_size)
	{
		state_recorder.init_recording_thread(nullptr);

		if (!fossilize_pipeline_data || fossilize_pipeline_size == 0)
			return;

		QM_LOG_INFO("Replaying cached state.\n");
		Fossilize::StateReplayer replayer;
		auto start = Util::get_current_time_nsecs();
		bool success = replayer.parse(*this, nullptr, fossilize_pipeline_data, fossilize_pipeline_size);
		auto end = Util::get_current_time_nsecs();
		QM_LOG_INFO("Completed replaying cached state in %.3f ms.\n", (end - start) * 1e-6);
		replayer_state = {};
		return success;
	}

	Util::RetainedHeapData Device::GetFossilizePipelineData()
	{
		uint8_t* serialized = nullptr;
		size_t serialized_size = 0;
		if (!state_recorder.serialize(&serialized, &serialized_size))
		{
			QM_LOG_INFO("Failed to serialize Fossilize state.\n");
		}

		Util::RetainedHeapData fossilize_pipeline_state_data = Util::CreateRetainedHeapData(serialized, serialized_size);

		state_recorder.free_serialized(serialized);

		return fossilize_pipeline_state_data;
	}

}