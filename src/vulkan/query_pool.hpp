#pragma once

#include "vulkan_headers.hpp"
#include "vulkan_common.hpp"
#include "utils/object_pool.hpp"

//Vulkan Profiling stuff

namespace Vulkan
{
	class Device;

	class PerformanceQueryPool
	{
	public:
		void init_device(Device* device, uint32_t queue_family_index);
		~PerformanceQueryPool();
		bool init_counters(const std::vector<std::string>& enable_counter_names);

		void begin_command_buffer(VkCommandBuffer cmd);
		void end_command_buffer(VkCommandBuffer cmd);

		void report();

		uint32_t get_num_counters() const;
		const VkPerformanceCounterKHR* get_available_counters() const;
		const VkPerformanceCounterDescriptionKHR* get_available_counter_descs() const;

	private:
		Device* device = nullptr;
		uint32_t queue_family_index = 0;
		VkQueryPool pool = VK_NULL_HANDLE;
		std::vector<VkPerformanceCounterResultKHR> results;
		std::vector<VkPerformanceCounterKHR> counters;
		std::vector<VkPerformanceCounterDescriptionKHR> counter_descriptions;
		std::vector<uint32_t> active_indices;
	};

	class QueryPoolResult;

	struct QueryPoolResultDeleter
	{
		void operator()(QueryPoolResult* query);
	};

	class QueryPoolResult : public Util::IntrusivePtrEnabled<QueryPoolResult, QueryPoolResultDeleter, HandleCounter>
	{
	public:
		friend struct QueryPoolResultDeleter;

		void signal_timestamp_ticks(uint64_t ticks)
		{
			timestamp_ticks = ticks;
			has_timestamp = true;
		}

		uint64_t get_timestamp_ticks() const
		{
			return timestamp_ticks;
		}

		bool is_signalled() const
		{
			return has_timestamp;
		}

	private:
		friend class Util::ObjectPool<QueryPoolResult>;

		explicit QueryPoolResult(Device* device_)
			: device(device_)
		{}

		Device* device;
		uint64_t timestamp_ticks = 0;
		bool has_timestamp = false;
	};

	using QueryPoolHandle = Util::IntrusivePtr<QueryPoolResult>;

	class QueryPool
	{
	public:
		explicit QueryPool(Device* device);

		~QueryPool();

		void begin();

		QueryPoolHandle write_timestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage);

	private:
		Device* device;
		const VolkDeviceTable& table;

		struct Pool
		{
			VkQueryPool pool = VK_NULL_HANDLE;
			std::vector<uint64_t> query_results;
			std::vector<QueryPoolHandle> cookies;
			unsigned index = 0;
			unsigned size = 0;
		};
		std::vector<Pool> pools;
		unsigned pool_index = 0;

		void add_pool();

		bool supports_timestamp = false;
	};

	class TimestampInterval : public Util::IntrusiveHashMapEnabled<TimestampInterval>
	{
	public:
		explicit TimestampInterval(std::string tag);

		void accumulate_time(double t);
		double get_time_per_iteration() const;
		const std::string& get_tag() const;
		void mark_end_of_frame_context();

		double get_total_time() const;
		uint64_t get_total_frame_iterations() const;
		uint64_t get_total_accumulations() const;

	private:
		std::string tag;
		double total_time = 0.0;
		uint64_t total_frame_iterations = 0;
		uint64_t total_accumulations = 0;
	};

	class TimestampIntervalManager
	{
	public:
		TimestampInterval* get_timestamp_tag(const char* tag);
		void mark_end_of_frame_context();

		void log_simple();

	private:
		Util::IntrusiveHashMap<TimestampInterval> timestamps;
	};
}
