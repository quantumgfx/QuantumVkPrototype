#include "wsi.hpp"
#include "quantumvk/vulkan/misc/quirks.hpp"
#include <thread>

using namespace std;

namespace Vulkan
{
	WSI::WSI()
	{
	}

	void WSIPlatform::SetWindowTitle(const string&)
	{
	}

	uintptr_t WSIPlatform::GetFullscreenMonitor()
	{
		return 0;
	}

	void WSI::SetWindowTitle(const string& title)
	{
		if (platform)
			platform->SetWindowTitle(title);
	}

	double WSI::GetSmoothElapsedTime() const
	{
		return smooth_elapsed_time;
	}

	double WSI::GetSmoothFrameTime() const
	{
		return smooth_frame_time;
	}

	float WSIPlatform::GetEstimatedFramePresentationDuration()
	{
		// Just assume 60 FPS for now.
		// TODO: Be more intelligent.
		return 1.0f / 60.0f;
	}

	float WSI::GetEstimatedVideoLatency()
	{
		if (using_display_timing)
		{
			// Very accurate estimate.
			double latency = timing.GetCurrentLatency();
			return float(latency);
		}
		else
		{
			// Very rough estimate.
			unsigned latency_frames = device->GetNumSwapchainImages();
			if (latency_frames > 0)
				latency_frames--;

			if (platform)
			{
				float frame_duration = platform->GetEstimatedFramePresentationDuration();
				return frame_duration * float(latency_frames);
			}
			else
				return -1.0f;
		}
	}

	bool WSI::InitExternalContext(std::unique_ptr<Context> fresh_context, uint8_t* initial_cache_data, size_t initial_cache_size)
	{
		context = std::move(fresh_context);

		// Need to have a dummy swapchain in place before we issue create device events.
		device.reset(new Device());
		device->SetContext(context.get(), initial_cache_data, initial_cache_size);
		device->InitExternalSwapchain({ SwapchainImages{ ImageHandle(nullptr), ImageViewHandle(nullptr)} });
		platform->EventDeviceCreated(device.get());
		table = &context->GetDeviceTable();
		return true;
	}

	bool WSI::InitExternalSwapchain(std::vector<SwapchainImages> swapchain_images_)
	{
		swapchain_width = platform->GetSurfaceWidth();
		swapchain_height = platform->GetSurfaceHeight();
		swapchain_aspect_ratio = platform->GetAspectRatio();

		external_swapchain_images = move(swapchain_images_);

		swapchain_width = external_swapchain_images.front().image->GetWidth();
		swapchain_height = external_swapchain_images.front().image->GetHeight();
		swapchain_format = external_swapchain_images.front().image->GetFormat();

		QM_LOG_INFO("Created swapchain %u x %u (fmt: %u).\n",
			swapchain_width, swapchain_height, static_cast<unsigned>(swapchain_format));

		platform->EventSwapchainDestroyed();
		platform->EventSwapchainCreated(device.get(), swapchain_width, swapchain_height, swapchain_aspect_ratio, external_swapchain_images.size(), swapchain_format, swapchain_current_prerotate);

		device->InitExternalSwapchain(this->external_swapchain_images);
		platform->GetFrameTimer().reset();
		external_acquire.Reset();
		external_release.Reset();
		return true;
	}

	void WSI::SetPlatform(WSIPlatform* platform_)
	{
		platform = platform_;
	}

	bool WSI::Init(unsigned num_thread_indices, uint8_t* initial_cache_data, size_t initial_cache_size, const char** instance_ext_, uint32_t instance_ext_count_, const char** device_ext_, uint32_t device_ext_count_)
	{
		auto instance_ext = platform->GetInstanceExtensions();
		auto device_ext = platform->GetDeviceExtensions();

		instance_ext.reserve(instance_ext.size() + instance_ext_count_);
		for (uint32_t i = 0; i < instance_ext_count_; i++)
		{
			instance_ext.push_back(*(instance_ext_ + i));
		}

		device_ext.reserve(device_ext.size() + device_ext_count_);
		for (uint32_t i = 0; i < device_ext_count_; i++)
		{
			device_ext.push_back(*(device_ext_ + i));
		}

		context.reset(new Context());

		context->SetNumThreadIndices(num_thread_indices);
		if (!context->InitInstanceAndDevice(instance_ext.data(), instance_ext.size(), device_ext.data(), device_ext.size()))
			return false;

		device.reset(new Device());
		device->SetContext(context.get(), initial_cache_data, initial_cache_size);
		table = &context->GetDeviceTable();

		platform->EventDeviceCreated(device.get());

		surface = platform->CreateSurface(context->GetInstance(), context->GetGPU());
		if (surface == VK_NULL_HANDLE)
			return false;

		unsigned width = platform->GetSurfaceWidth();
		unsigned height = platform->GetSurfaceHeight();
		swapchain_aspect_ratio = platform->GetAspectRatio();

		VkBool32 supported = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(context->GetGPU(), context->GetGraphicsQueueFamily(), surface, &supported);
		if (!supported)
			return false;

		if (!BlockingInitSwapchain(width, height))
			return false;

		device->InitSwapchain(swapchain_images, swapchain_width, swapchain_height, swapchain_format);
		platform->GetFrameTimer().reset();
		return true;
	}

	void WSI::InitSurfaceAndSwapchain(VkSurfaceKHR new_surface)
	{
		QM_LOG_INFO("init_surface_and_swapchain()\n");
		if (new_surface != VK_NULL_HANDLE)
		{
			VK_ASSERT(surface == VK_NULL_HANDLE);
			surface = new_surface;
		}

		swapchain_width = platform->GetSurfaceWidth();
		swapchain_height = platform->GetSurfaceHeight();
		UpdateFramebuffer(swapchain_width, swapchain_height);
	}

	void WSI::DrainSwapchain()
	{
		release_semaphores.clear();
		device->SetAcquireSemaphore(0, Semaphore{});
		device->ConsumeReleaseSemaphore();
		device->WaitIdle();
	}

	void WSI::TearDownSwapchain()
	{
		DrainSwapchain();

		if (swapchain != VK_NULL_HANDLE)
			table->vkDestroySwapchainKHR(context->GetDevice(), swapchain, nullptr);
		swapchain = VK_NULL_HANDLE;
		has_acquired_swapchain_index = false;
	}

	void WSI::DeinitSurfaceAndSwapchain()
	{
		QM_LOG_INFO("deinit_surface_and_swapchain()\n");

		TearDownSwapchain();

		if (surface != VK_NULL_HANDLE)
			vkDestroySurfaceKHR(context->GetInstance(), surface, nullptr);
		surface = VK_NULL_HANDLE;

		platform->EventSwapchainDestroyed();
	}

	void WSI::SetExternalFrame(unsigned index, Semaphore acquire_semaphore, double frame_time)
	{
		external_frame_index = index;
		external_acquire = move(acquire_semaphore);
		frame_is_external = true;
		external_frame_time = frame_time;
	}

	bool WSI::BeginFrameExternal()
	{
		device->NextFrameContext();

		// Need to handle this stuff from outside.
		if (has_acquired_swapchain_index)
			return false;

		auto frame_time = platform->GetFrameTimer().frame(external_frame_time);
		auto elapsed_time = platform->GetFrameTimer().get_elapsed();

		// Assume we have been given a smooth frame pacing.
		smooth_frame_time = frame_time;
		smooth_elapsed_time = elapsed_time;

		// Poll after acquire as well for optimal latency.
		platform->PollInput();

		swapchain_index = external_frame_index;
		platform->EventFrameTick(frame_time, elapsed_time);

		platform->EventSwapchainIndex(device.get(), swapchain_index);
		device->SetAcquireSemaphore(swapchain_index, external_acquire);
		external_acquire.Reset();
		return true;
	}

	Semaphore WSI::ConsumeExternalReleaseSemaphore()
	{
		Semaphore sem;
		std::swap(external_release, sem);
		return sem;
	}

	//#define VULKAN_WSI_TIMING_DEBUG

	bool WSI::BeginFrame()
	{
		if (frame_is_external)
			return BeginFrameExternal();

#ifdef VULKAN_WSI_TIMING_DEBUG
		auto next_frame_start = Util::get_current_time_nsecs();
#endif

		device->NextFrameContext();

#ifdef VULKAN_WSI_TIMING_DEBUG
		auto next_frame_end = Util::get_current_time_nsecs();
		LOGI("Waited for vacant frame context for %.3f ms.\n", (next_frame_end - next_frame_start) * 1e-6);
#endif

		if (swapchain == VK_NULL_HANDLE || platform->ShouldResize())
		{
			UpdateFramebuffer(platform->GetSurfaceWidth(), platform->GetSurfaceHeight());
			platform->AcknowledgeResize();
		}

		if (swapchain == VK_NULL_HANDLE)
		{
			QM_LOG_ERROR("Completely lost swapchain. Cannot continue.\n");
			return false;
		}

		if (has_acquired_swapchain_index)
		{
			// Poll input because this is supossed to be called every frame
			platform->PollInput();
			return true;
		}

		external_release.Reset();

		VkResult result;
		do
		{
			auto acquire = device->RequestLegacySemaphore();

			// For adaptive low latency we don't want to observe the time it takes to wait for
			// WSI semaphore as part of our latency,
			// which means we will never get sub-frame latency on some implementations,
			// so block on that first.
			Fence fence;
			if (timing.GetOptions().latency_limiter == LatencyLimiter::AdaptiveLowLatency)
				fence = device->RequestLegacyFence();

#ifdef VULKAN_WSI_TIMING_DEBUG
			auto acquire_start = Util::get_current_time_nsecs();
#endif

			result = table->vkAcquireNextImageKHR(context->GetDevice(), swapchain, UINT64_MAX, acquire->GetSemaphore(),
				fence ? fence->GetFence() : VK_NULL_HANDLE, &swapchain_index);

#ifdef ANDROID
			// Android 10 can return suboptimal here, only because of pre-transform.
			// We don't care about that, and treat this as success.
			if (result == VK_SUBOPTIMAL_KHR)
				result = VK_SUCCESS;
#endif

			if (result == VK_SUCCESS && fence)
				fence->Wait();

			if (result == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
			{
				QM_LOG_ERROR("Lost exclusive full-screen ...\n");
			}

#ifdef VULKAN_WSI_TIMING_DEBUG
			auto acquire_end = Util::get_current_time_nsecs();
			LOGI("vkAcquireNextImageKHR took %.3f ms.\n", (acquire_end - acquire_start) * 1e-6);
#endif

			if (result == VK_SUCCESS)
			{
				has_acquired_swapchain_index = true;
				acquire->SignalExternal();

				auto frame_time = platform->GetFrameTimer().frame();
				auto elapsed_time = platform->GetFrameTimer().get_elapsed();

				if (using_display_timing)
					timing.BeginFrame(frame_time, elapsed_time);

				smooth_frame_time = frame_time;
				smooth_elapsed_time = elapsed_time;

				// Poll after acquire as well for optimal latency.
				platform->PollInput();
				platform->EventFrameTick(frame_time, elapsed_time);

				platform->EventSwapchainIndex(device.get(), swapchain_index);

				if (device->GetWorkarounds().wsi_acquire_barrier_is_expensive)
				{
					// Acquire async. Use the async graphics queue, as it's most likely not being used right away.
					device->AddWaitSemaphore(CommandBuffer::Type::AsyncGraphics, acquire, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, true);
					auto cmd = device->RequestCommandBuffer(CommandBuffer::Type::AsyncGraphics);
					cmd->ImageBarrier(device->GetSwapchainView(swapchain_index).GetImage(),
						VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
						VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0);

					// Get a new acquire semaphore.
					acquire.Reset();
					device->Submit(cmd, nullptr, 1, &acquire);
				}

				device->SetAcquireSemaphore(swapchain_index, acquire);
			}
			else if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
			{
				VK_ASSERT(swapchain_width != 0);
				VK_ASSERT(swapchain_height != 0);

				TearDownSwapchain();

				if (!BlockingInitSwapchain(swapchain_width, swapchain_height))
					return false;
				device->InitSwapchain(swapchain_images, swapchain_width, swapchain_height, swapchain_format);
			}
			else
			{
				return false;
			}
		} while (result != VK_SUCCESS);
		return true;
	}

	bool WSI::EndFrame()
	{
		device->EndFrameContext();

		// Take ownership of the release semaphore so that the external user can use it.
		if (frame_is_external)
		{
			// If we didn't render into the swapchain this frame, we will return a blank semaphore.
			external_release = device->ConsumeReleaseSemaphore();
			if (external_release && !external_release->IsSignalled())
				abort();
			frame_is_external = false;
		}
		else
		{
			if (!device->SwapchainTouched())
				return true;

			has_acquired_swapchain_index = false;

			auto release = device->ConsumeReleaseSemaphore();
			VK_ASSERT(release);
			VK_ASSERT(release->IsSignalled());
			auto release_semaphore = release->GetSemaphore();
			VK_ASSERT(release_semaphore != VK_NULL_HANDLE);

			VkResult result = VK_SUCCESS;
			VkPresentInfoKHR info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
			info.waitSemaphoreCount = 1;
			info.pWaitSemaphores = &release_semaphore;
			info.swapchainCount = 1;
			info.pSwapchains = &swapchain;
			info.pImageIndices = &swapchain_index;
			info.pResults = &result;

			VkPresentTimeGOOGLE present_time;
			VkPresentTimesInfoGOOGLE present_timing = { VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE };
			present_timing.swapchainCount = 1;
			present_timing.pTimes = &present_time;

			if (using_display_timing && timing.FillPresentInfoTiming(present_time))
			{
				info.pNext = &present_timing;
			}

#ifdef VULKAN_WSI_TIMING_DEBUG
			auto present_start = Util::get_current_time_nsecs();
#endif

			//auto present_ts = device->write_calibrated_timestamp();
			VkResult overall = table->vkQueuePresentKHR(context->GetGraphicsQueue(), &info);
			//device->register_time_interval("WSI", std::move(present_ts), device->write_calibrated_timestamp(), "present");

#ifdef ANDROID
			// Android 10 can return suboptimal here, only because of pre-transform.
			// We don't care about that, and treat this as success.
			if (overall == VK_SUBOPTIMAL_KHR)
				overall = VK_SUCCESS;
			if (result == VK_SUBOPTIMAL_KHR)
				result = VK_SUCCESS;
#endif

			if (overall == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT || result == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
			{
				QM_LOG_ERROR("Lost exclusive full-screen ...\n");
			}

#ifdef VULKAN_WSI_TIMING_DEBUG
			auto present_end = Util::get_current_time_nsecs();
			QM_LOG_INFO("vkQueuePresentKHR took %.3f ms.\n", (present_end - present_start) * 1e-6);
#endif

			if (overall != VK_SUCCESS || result != VK_SUCCESS)
			{
				QM_LOG_ERROR("vkQueuePresentKHR failed.\n");
				TearDownSwapchain();
				return false;
			}
			else
			{
				release->WaitExternal();
				// Cannot release the WSI wait semaphore until we observe that the image has been
				// waited on again.
				release_semaphores[swapchain_index] = release;
			}

			// Re-init swapchain.
			if (present_mode != current_present_mode || srgb_backbuffer_enable != current_srgb_backbuffer_enable)
			{
				current_present_mode = present_mode;
				current_srgb_backbuffer_enable = srgb_backbuffer_enable;
				UpdateFramebuffer(swapchain_width, swapchain_height);
			}
		}

		return true;
	}

	void WSI::UpdateFramebuffer(unsigned width, unsigned height)
	{
		if (context && device)
		{
			DrainSwapchain();
			if (BlockingInitSwapchain(width, height))
				device->InitSwapchain(swapchain_images, swapchain_width, swapchain_height, swapchain_format);
		}
	}

	void WSI::SetPresentMode(PresentMode mode)
	{
		present_mode = mode;
		if (!has_acquired_swapchain_index && present_mode != current_present_mode)
		{
			current_present_mode = present_mode;
			UpdateFramebuffer(swapchain_width, swapchain_height);
		}
	}

	void WSI::SetBackbufferSrgb(bool enable)
	{
		srgb_backbuffer_enable = enable;
		if (!has_acquired_swapchain_index && srgb_backbuffer_enable != current_srgb_backbuffer_enable)
		{
			current_srgb_backbuffer_enable = srgb_backbuffer_enable;
			UpdateFramebuffer(swapchain_width, swapchain_height);
		}
	}

	void WSI::DeinitExternal()
	{
		if (platform)
			platform->ReleaseResources();

		if (context)
		{
			TearDownSwapchain();
			platform->EventSwapchainDestroyed();
		}

		if (surface != VK_NULL_HANDLE)
			vkDestroySurfaceKHR(context->GetInstance(), surface, nullptr);

		if (platform)
			platform->EventDeviceDestroyed();
		external_release.Reset();
		external_acquire.Reset();
		external_swapchain_images.clear();
		device.reset();
		context.reset();

		using_display_timing = false;
	}

	static inline const char* PresentModeToString(VkPresentModeKHR mode)
	{
		switch (mode)
		{
		case VK_PRESENT_MODE_IMMEDIATE_KHR:                 return "Immediate";
		case VK_PRESENT_MODE_MAILBOX_KHR:                   return "Mailbox";
		case VK_PRESENT_MODE_FIFO_KHR:                      return "Fifo";
		case VK_PRESENT_MODE_FIFO_RELAXED_KHR:              return "Fifo Relaxed";
		case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:     return "Shared demand refresh";
		case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: return "Shared continuous refresh";
		default:                                            return "Unknown";
		}
	}

	bool WSI::BlockingInitSwapchain(unsigned width, unsigned height)
	{
		SwapchainError err;
		unsigned retry_counter = 0;
		do
		{
			swapchain_aspect_ratio = platform->GetAspectRatio();
			err = InitSwapchain(width, height);
			if (err == SwapchainError::Error)
			{
				if (++retry_counter > 3)
					return false;

				// Try to not reuse the swapchain.
				TearDownSwapchain();
			}
			else if (err == SwapchainError::NoSurface && platform->Alive(*this))
			{
				platform->PollInput();
				this_thread::sleep_for(chrono::milliseconds(10));
			}
		} while (err != SwapchainError::None);

		return swapchain != VK_NULL_HANDLE;
	}

	WSI::SwapchainError WSI::InitSwapchain(unsigned width, unsigned height)
	{
		if (surface == VK_NULL_HANDLE)
		{
			QM_LOG_ERROR("Cannot create swapchain with surface == VK_NULL_HANDLE.\n");
			return SwapchainError::Error;
		}

		VkSurfaceCapabilitiesKHR surface_properties;
		VkPhysicalDeviceSurfaceInfo2KHR surface_info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR };
		surface_info.surface = surface;
		bool use_surface_info = device->GetDeviceExtensions().supports_surface_capabilities2;
		bool use_application_controlled_exclusive_fullscreen = false;

#ifdef _WIN32
		VkSurfaceFullScreenExclusiveInfoEXT exclusive_info = { VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT };
		VkSurfaceFullScreenExclusiveWin32InfoEXT exclusive_info_win32 = { VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT };

		HMONITOR monitor = reinterpret_cast<HMONITOR>(platform->GetFullscreenMonitor());
		if (!device->GetDeviceExtensions().supports_full_screen_exclusive)
			monitor = nullptr;

		surface_info.pNext = &exclusive_info;
		if (monitor != nullptr)
		{
			exclusive_info.pNext = &exclusive_info_win32;
			exclusive_info_win32.hmonitor = monitor;
			QM_LOG_INFO("Win32: Got a full-screen monitor.\n");
		}
		else
			QM_LOG_INFO("Win32: Not running full-screen.\n");

		if (prefer_exclusive_full_screen)
		{
			QM_LOG_INFO("Win32: Opting in to exclusive full-screen!\n");
			exclusive_info.fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_ALLOWED_EXT;
		}
		else
		{
			QM_LOG_INFO("Win32: Opting out of exclusive full-screen!\n");
			exclusive_info.fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT;
		}
#endif

		auto gpu = context->GetGPU();
		if (use_surface_info)
		{
			VkSurfaceCapabilities2KHR surface_capabilities2 = { VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR };

#ifdef _WIN32
			VkSurfaceCapabilitiesFullScreenExclusiveEXT capability_full_screen_exclusive = { VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_FULL_SCREEN_EXCLUSIVE_EXT };
			if (device->GetDeviceExtensions().supports_full_screen_exclusive && exclusive_info_win32.hmonitor)
			{
				surface_capabilities2.pNext = &capability_full_screen_exclusive;
				capability_full_screen_exclusive.pNext = &exclusive_info_win32;
			}
#endif

			if (vkGetPhysicalDeviceSurfaceCapabilities2KHR(gpu, &surface_info, &surface_capabilities2) != VK_SUCCESS)
				return SwapchainError::Error;

			surface_properties = surface_capabilities2.surfaceCapabilities;

#ifdef _WIN32
			if (capability_full_screen_exclusive.fullScreenExclusiveSupported)
				QM_LOG_INFO("Surface could support app-controlled exclusive fullscreen.\n");

			use_application_controlled_exclusive_fullscreen = exclusive_info.fullScreenExclusive == VK_FULL_SCREEN_EXCLUSIVE_ALLOWED_EXT && capability_full_screen_exclusive.fullScreenExclusiveSupported == VK_TRUE;
			if (monitor == nullptr)
				use_application_controlled_exclusive_fullscreen = false;
#endif

			if (use_application_controlled_exclusive_fullscreen)
			{
				QM_LOG_INFO("Using app-controlled exclusive fullscreen.\n");
#ifdef _WIN32
				exclusive_info.fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;
				exclusive_info.pNext = &exclusive_info_win32;
#endif
			}
			else
			{
				QM_LOG_INFO("Not using app-controlled exclusive fullscreen.\n");
			}
		}
		else
		{
			if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surface_properties) != VK_SUCCESS)
				return SwapchainError::Error;
		}

		// Happens on nVidia Windows when you minimize a window.
		if (surface_properties.maxImageExtent.width == 0 && surface_properties.maxImageExtent.height == 0)
			return SwapchainError::NoSurface;

		uint32_t format_count;
		vector<VkSurfaceFormatKHR> formats;

		if (use_surface_info)
		{
			if (vkGetPhysicalDeviceSurfaceFormats2KHR(gpu, &surface_info, &format_count, nullptr) != VK_SUCCESS)
				return SwapchainError::Error;

			vector<VkSurfaceFormat2KHR> formats2(format_count);

			for (auto& f : formats2)
			{
				f = {};
				f.sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR;
			}

			if (vkGetPhysicalDeviceSurfaceFormats2KHR(gpu, &surface_info, &format_count, formats2.data()) != VK_SUCCESS)
				return SwapchainError::Error;

			formats.reserve(format_count);
			for (auto& f : formats2)
				formats.push_back(f.surfaceFormat);
		}
		else
		{
			if (vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, nullptr) != VK_SUCCESS)
				return SwapchainError::Error;
			formats.resize(format_count);
			if (vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, formats.data()) != VK_SUCCESS)
				return SwapchainError::Error;
		}

		VkSurfaceFormatKHR format;
		if (format_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
		{
			format = formats[0];
			format.format = VK_FORMAT_B8G8R8A8_UNORM;
		}
		else
		{
			if (format_count == 0)
			{
				QM_LOG_ERROR("Surface has no formats.\n");
				return SwapchainError::Error;
			}

			bool found = false;
			for (unsigned i = 0; i < format_count; i++)
			{
				if (current_srgb_backbuffer_enable)
				{
					if (formats[i].format == VK_FORMAT_R8G8B8A8_SRGB || formats[i].format == VK_FORMAT_B8G8R8A8_SRGB || formats[i].format == VK_FORMAT_A8B8G8R8_SRGB_PACK32)
					{
						format = formats[i];
						found = true;
					}
				}
				else
				{
					if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM || formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_A8B8G8R8_UNORM_PACK32)
					{
						format = formats[i];
						found = true;
					}
				}
			}

			if (!found)
				format = formats[0];
		}

		static const char* transform_names[] = {
			"IDENTITY_BIT_KHR",
			"ROTATE_90_BIT_KHR",
			"ROTATE_180_BIT_KHR",
			"ROTATE_270_BIT_KHR",
			"HORIZONTAL_MIRROR_BIT_KHR",
			"HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR",
			"HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR",
			"HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR",
			"INHERIT_BIT_KHR",
		};

		QM_LOG_INFO("Current transform is enum 0x%x.\n", unsigned(surface_properties.currentTransform));

		for (unsigned i = 0; i <= 8; i++)
		{
			if (surface_properties.supportedTransforms & (1u << i))
				QM_LOG_INFO("Supported transform 0x%x: %s.\n", 1u << i, transform_names[i]);
		}

		VkSurfaceTransformFlagBitsKHR pre_transform;
		if (!support_prerotate && (surface_properties.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0)
			pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		else
			pre_transform = surface_properties.currentTransform;

		if (pre_transform != surface_properties.currentTransform)
		{
			QM_LOG_WARN("surfaceTransform (0x%x) != currentTransform (0x%u). Might get performance penalty.\n",
				unsigned(pre_transform), unsigned(surface_properties.currentTransform));
		}

		swapchain_current_prerotate = pre_transform;

		VkExtent2D swapchain_size;
		QM_LOG_INFO("Swapchain current extent: %d x %d\n", int(surface_properties.currentExtent.width), int(surface_properties.currentExtent.height));

		// Try to match the swapchain size up with what we expect.
		float target_aspect_ratio = float(width) / float(height);
		if ((swapchain_aspect_ratio > 1.0f && target_aspect_ratio < 1.0f) ||
			(swapchain_aspect_ratio < 1.0f && target_aspect_ratio > 1.0f))
		{
			swap(width, height);
		}

		// If we are using pre-rotate of 90 or 270 degrees, we need to flip width and height again.
		if (swapchain_current_prerotate &
			(VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR |
				VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR |
				VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR))
		{
			swap(width, height);
		}

		// Clamp the target width, height to boundaries.
		swapchain_size.width =
			max(min(width, surface_properties.maxImageExtent.width), surface_properties.minImageExtent.width);
		swapchain_size.height =
			max(min(height, surface_properties.maxImageExtent.height), surface_properties.minImageExtent.height);

		uint32_t num_present_modes;

		std::vector<VkPresentModeKHR> present_modes;

#ifdef _WIN32
		if (use_surface_info && device->GetDeviceExtensions().supports_full_screen_exclusive)
		{
			if (vkGetPhysicalDeviceSurfacePresentModes2EXT(gpu, &surface_info, &num_present_modes, nullptr) != VK_SUCCESS)
				return SwapchainError::Error;
			present_modes.resize(num_present_modes);
			if (vkGetPhysicalDeviceSurfacePresentModes2EXT(gpu, &surface_info, &num_present_modes, present_modes.data()) !=
				VK_SUCCESS)
				return SwapchainError::Error;
		}
		else
#endif
		{
			if (vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &num_present_modes, nullptr) != VK_SUCCESS)
				return SwapchainError::Error;
			present_modes.resize(num_present_modes);
			if (vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &num_present_modes, present_modes.data()) != VK_SUCCESS)
				return SwapchainError::Error;
		}

		VkPresentModeKHR swapchain_present_mode = VK_PRESENT_MODE_FIFO_KHR;
		bool use_vsync = current_present_mode == PresentMode::SyncToVBlank;
		if (!use_vsync)
		{
			bool allow_mailbox = current_present_mode != PresentMode::UnlockedForceTearing;
			bool allow_immediate = current_present_mode != PresentMode::UnlockedNoTearing;

#ifdef _WIN32
			if (device->GetGPUProperties().vendorID == VENDOR_ID_NVIDIA)
			{
				// If we're trying to go exclusive full-screen,
				// we need to ban certain types of present modes which apparently do not work as we expect.
				if (use_application_controlled_exclusive_fullscreen)
					allow_mailbox = false;
				else
					allow_immediate = false;
			}
#endif

			for (uint32_t i = 0; i < num_present_modes; i++)
			{
				if ((allow_immediate && present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) || (allow_mailbox && present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR))
				{
					swapchain_present_mode = present_modes[i];
					break;
				}
			}
		}

		QM_LOG_INFO("Swapchain Present Mode: %s\n", PresentModeToString(swapchain_present_mode));
		QM_LOG_INFO("Targeting %u swapchain images.\n", desired_swapchain_images);

		if (desired_swapchain_images < surface_properties.minImageCount)
			desired_swapchain_images = surface_properties.minImageCount;

		if ((surface_properties.maxImageCount > 0) && (desired_swapchain_images > surface_properties.maxImageCount))
			desired_swapchain_images = surface_properties.maxImageCount;

		VkCompositeAlphaFlagBitsKHR composite_mode = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		if (surface_properties.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
			composite_mode = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
		if (surface_properties.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
			composite_mode = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		if (surface_properties.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
			composite_mode = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
		if (surface_properties.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
			composite_mode = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;

		VkSwapchainKHR old_swapchain = swapchain;

		VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		info.surface = surface;
		info.minImageCount = desired_swapchain_images;
		info.imageFormat = format.format;
		info.imageColorSpace = format.colorSpace;
		info.imageExtent.width = swapchain_size.width;
		info.imageExtent.height = swapchain_size.height;
		info.imageArrayLayers = 1;
		info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.preTransform = pre_transform;
		info.compositeAlpha = composite_mode;
		info.presentMode = swapchain_present_mode;
		info.clipped = VK_TRUE;
		info.oldSwapchain = old_swapchain;

#ifdef _WIN32
		if (device->GetDeviceExtensions().supports_full_screen_exclusive)
			info.pNext = &exclusive_info;
#endif

		auto res = table->vkCreateSwapchainKHR(context->GetDevice(), &info, nullptr, &swapchain);
		if (old_swapchain != VK_NULL_HANDLE)
			table->vkDestroySwapchainKHR(context->GetDevice(), old_swapchain, nullptr);
		has_acquired_swapchain_index = false;

#ifdef _WIN32
		if (use_application_controlled_exclusive_fullscreen)
		{
			bool success = vkAcquireFullScreenExclusiveModeEXT(context->GetDevice(), swapchain) == VK_SUCCESS;
			if (success)
				QM_LOG_INFO("Successfully acquired exclusive full-screen.\n");
			else
				QM_LOG_INFO("Failed to acquire exclusive full-screen. Using borderless windowed.\n");
		}
#endif

#if 0
		if (use_vsync && context->GetEnabledDeviceFetures().supports_google_display_timing)
		{
			WSITimingOptions timing_options;
			timing_options.swap_interval = 1;
			//timing_options.adaptive_swap_interval = true;
			//timing_options.latency_limiter = LatencyLimiter::IdealPipeline;
			timing.init(platform, device.get(), swapchain, timing_options);
			using_display_timing = true;
		}
		else
#endif
			using_display_timing = false;

		if (res != VK_SUCCESS)
		{
			QM_LOG_ERROR("Failed to create swapchain (code: %d)\n", int(res));
			swapchain = VK_NULL_HANDLE;
			return SwapchainError::Error;
		}

		swapchain_width = swapchain_size.width;
		swapchain_height = swapchain_size.height;
		swapchain_format = format.format;

		QM_LOG_INFO("Created swapchain %u x %u (fmt: %u).\n", swapchain_width, swapchain_height,
			static_cast<unsigned>(swapchain_format));

		uint32_t image_count;
		if (table->vkGetSwapchainImagesKHR(context->GetDevice(), swapchain, &image_count, nullptr) != VK_SUCCESS)
			return SwapchainError::Error;
		swapchain_images.resize(image_count);
		release_semaphores.resize(image_count);
		if (table->vkGetSwapchainImagesKHR(context->GetDevice(), swapchain, &image_count, swapchain_images.data()) != VK_SUCCESS)
			return SwapchainError::Error;

		QM_LOG_INFO("Got %u swapchain images.\n", image_count);

		platform->EventSwapchainDestroyed();
		platform->EventSwapchainCreated(device.get(), swapchain_width, swapchain_height, swapchain_aspect_ratio, image_count, info.imageFormat, swapchain_current_prerotate);

		return SwapchainError::None;
	}

	double WSI::GetEstimatedRefreshInterval() const
	{
		uint64_t interval = timing.GetRefreshInterval();
		if (interval)
			return interval * 1e-9;
		else if (platform)
			return platform->GetEstimatedFramePresentationDuration();
		else
			return 0.0;
	}

	void WSI::SetSupportPrerotate(bool enable)
	{
		support_prerotate = enable;
	}

	VkSurfaceTransformFlagBitsKHR WSI::GetCurrentPrerotate() const
	{
		return swapchain_current_prerotate;
	}

	void WSI::PreferredNumSwapchainImages(uint32_t preferred_swapchain_images)
	{
		desired_swapchain_images = preferred_swapchain_images;
	}

	void WSI::PreferExclusiveFullScreen(bool prefer)
	{
		prefer_exclusive_full_screen = prefer;
	}

	WSI::~WSI()
	{
		DeinitExternal();
	}

	void WSI::BuildPrerotateMatrix2x2(VkSurfaceTransformFlagBitsKHR pre_rotate, float mat[4])
	{
		// TODO: HORIZONTAL_MIRROR.
		switch (pre_rotate)
		{
		default:
			mat[0] = 1.0f;
			mat[1] = 0.0f;
			mat[2] = 0.0f;
			mat[3] = 1.0f;
			break;

		case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
			mat[0] = 0.0f;
			mat[1] = 1.0f;
			mat[2] = -1.0f;
			mat[3] = 0.0f;
			break;

		case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
			mat[0] = 0.0f;
			mat[1] = -1.0f;
			mat[2] = 1.0f;
			mat[3] = 0.0f;
			break;

		case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
			mat[0] = -1.0f;
			mat[1] = 0.0f;
			mat[2] = 0.0f;
			mat[3] = -1.0f;
			break;
		}
	}

	void WSIPlatform::EventDeviceCreated(Device*) {}
	void WSIPlatform::EventDeviceDestroyed() {}
	void WSIPlatform::EventSwapchainCreated(Device*, unsigned, unsigned, float, size_t, VkFormat, VkSurfaceTransformFlagBitsKHR) {}
	void WSIPlatform::EventSwapchainDestroyed() {}
	void WSIPlatform::EventFrameTick(double, double) {}
	void WSIPlatform::EventSwapchainIndex(Device*, unsigned) {}
	void WSIPlatform::EventDisplayTimingStutter(uint32_t, uint32_t, uint32_t) {}

}
