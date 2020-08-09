#pragma once

#include "vulkan/vulkan_headers.hpp"
#include <vector>

namespace Vulkan
{
	enum class LatencyLimiter
	{
		None,
		AdaptiveLowLatency,
		IdealPipeline
	};

	struct WSITimingOptions
	{
		uint32_t swap_interval = 1;
		LatencyLimiter latency_limiter = LatencyLimiter::None;
		bool adaptive_swap_interval = false;
		bool debug = false;
	};

	class WSIPlatform;
	class Device;

	class WSITiming
	{
	public:
		void Init(WSIPlatform* platform, Device* device, VkSwapchainKHR swapchain, const WSITimingOptions& options = {});
		void BeginFrame(double& frame_time, double& elapsed_time);

		bool FillPresentInfoTiming(VkPresentTimeGOOGLE& time);
		double GetCurrentLatency() const;

		void SetSwapInterval(unsigned interval);
		void SetDebugEnable(bool enable);
		void SetLatencyLimiter(LatencyLimiter limiter);

		// Can return 0 if we don't know the refresh interval yet.
		uint64_t GetRefreshInterval() const;

		const WSITimingOptions& GetOptions() const;

	private:
		WSIPlatform* platform = nullptr;
		VkDevice device = VK_NULL_HANDLE;
		const VolkDeviceTable* table = nullptr;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		WSITimingOptions options;

		enum { NUM_TIMINGS = 32, NUM_TIMING_MASK = NUM_TIMINGS - 1 };

		struct Serial
		{
			uint32_t serial = 0;
		} serial_info;

		enum class TimingResult
		{
			Unknown,
			VeryEarly,
			TooLate,
			Expected
		};

		struct Timing
		{
			uint32_t wall_serial = 0;
			uint64_t wall_frame_begin = 0;
			uint64_t wall_frame_target = 0;
			uint32_t swap_interval_target = 0;
			TimingResult result = TimingResult::Unknown;
			int64_t slack = 0;
			int64_t pipeline_latency = 0;
			VkPastPresentationTimingGOOGLE timing = {};
		};

		struct Feedback
		{
			uint64_t refresh_interval = 0;
			Timing past_timings[NUM_TIMINGS];
			std::vector<VkPastPresentationTimingGOOGLE> timing_buffer;
			double latency = 0.0;
		} feedback;

		struct Pacing
		{
			uint32_t base_serial = 0;
			uint64_t base_present = 0;
			bool have_estimate = false;
			bool have_real_estimate = false;
		} pacing;

		struct FrameTimer
		{
			uint64_t present_time = 0;
			uint64_t serial = 0;
		} last_frame;

		struct SmoothTimer
		{
			double elapsed = 0.0;
			double offset = 0.0;
		} smoothing;

		uint64_t ComputeTargetPresentTimeForSerial(uint32_t serial);
		uint64_t GetWallTime();
		void UpdatePastPresentationTiming();
		Timing* FindLatestTimestamp(uint32_t start_serial);
		void UpdateFramePacing(uint32_t id, uint64_t present_time, bool wall_time);
		void UpdateRefreshInterval();
		void UpdateFrameTimeSmoothing(double& frame_time, double& elapsed_time);
		bool GetConservativeLatency(int64_t& latency) const;
		void WaitUntil(int64_t nsecs);
		void LimitLatency(Timing& new_timing);
		void PromoteOrDemoteFrameRate();
	};
}
