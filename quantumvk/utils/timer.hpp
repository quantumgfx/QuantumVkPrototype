#pragma once

#include <cstdint>

namespace Util
{
	//Frame timer
	class FrameTimer
	{
	public:
		FrameTimer();

		void reset();
		double frame();
		double frame(double frame_time);
		double get_elapsed() const;
		double get_frame_time() const;

		void enter_idle();
		void leave_idle();

	private:
		int64_t start;
		int64_t last;
		int64_t last_period;
		int64_t idle_start;
		int64_t idle_time = 0;
		int64_t get_time();
	};

	//Simple Timer class
	class Timer
	{
	public:
		void start();
		double end();

	private:
		int64_t t = 0;
	};

	int64_t get_current_time_nsecs();
}