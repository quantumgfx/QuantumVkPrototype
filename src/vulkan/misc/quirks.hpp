#pragma once

namespace Vulkan
{
	//Structs specifying various implementation quirks and work arounds.

	struct ImplementationQuirks
	{
		bool instance_deferred_lights = true;
		bool merge_subpasses = true;
		bool use_transient_color = true;
		bool use_transient_depth_stencil = true;
		bool clustering_list_iteration = false;
		bool clustering_force_cpu = false;
		bool queue_wait_on_submission = false;
		bool staging_need_device_local = false;
		bool use_async_compute_post = true;
		bool render_graph_force_single_queue = false;
		bool force_no_subgroups = false;

		static ImplementationQuirks& get()
		{
			static ImplementationQuirks quirks;
			return quirks;
		}
	};

	struct ImplementationWorkarounds
	{
		bool emulate_event_as_pipeline_barrier = false;
		bool wsi_acquire_barrier_is_expensive = false;
		bool optimize_all_graphics_barrier = false;
		bool force_store_in_render_pass = false;
		bool broken_color_write_mask = false;
	};
}
