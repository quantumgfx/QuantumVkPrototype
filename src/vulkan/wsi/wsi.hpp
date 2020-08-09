#pragma once

#include "utils/timer.hpp"

#include "vulkan/device.hpp"
#include "vulkan/vulkan_headers.hpp"

#include "vulkan/sync/semaphore_manager.hpp"

#include "wsi_timing.hpp"

#include <memory>
#include <vector>

namespace Vulkan
{
	class WSI;

	class WSIPlatform
	{
	public:
		virtual ~WSIPlatform() = default;

		virtual VkSurfaceKHR CreateSurface(VkInstance instance, VkPhysicalDevice gpu) = 0;
		virtual std::vector<const char*> GetInstanceExtensions() = 0;
		virtual std::vector<const char*> GetDeviceExtensions()
		{
			return { "VK_KHR_swapchain" };
		}

		virtual VkFormat GetPreferredFormat()
		{
			return VK_FORMAT_B8G8R8A8_SRGB;
		}

		bool ShouldResize()
		{
			return resize;
		}

		void AcknowledgeResize()
		{
			resize = false;
		}

		virtual uint32_t GetSurfaceWidth() = 0;
		virtual uint32_t GetSurfaceHeight() = 0;

		virtual float GetAspectRatio()
		{
			return float(GetSurfaceWidth()) / float(GetSurfaceHeight());
		}

		virtual bool Alive(Vulkan::WSI& wsi) = 0;
		virtual void PollInput() = 0;
		virtual bool HasExternalSwapchain()
		{
			return false;
		}

		Util::FrameTimer& GetFrameTimer()
		{
			return timer;
		}

		virtual void ReleaseResources()
		{
		}

		virtual void EventDeviceCreated(Device* device);
		virtual void EventDeviceDestroyed();
		virtual void EventSwapchainCreated(Device* device, unsigned width, unsigned height, float aspect_ratio, size_t num_swapchain_images, VkFormat format, VkSurfaceTransformFlagBitsKHR pre_rotate);
		virtual void EventSwapchainDestroyed();
		virtual void EventFrameTick(double frame, double elapsed);
		virtual void EventSwapchainTndex(Device* device, unsigned index);
		virtual void EventDisplayTimingStutter(uint32_t current_serial, uint32_t observed_serial, unsigned dropped_frames);

		virtual float GetEstimatedFramePresentationDuration();

		virtual void SetWindowTitle(const std::string& title);

		virtual uintptr_t GetFullscreenMonitor();

	protected:
		bool resize = false;

	private:
		Util::FrameTimer timer;
	};

	enum class PresentMode
	{
		SyncToVBlank, // Force FIFO
		UnlockedMaybeTear, // MAILBOX or IMMEDIATE
		UnlockedForceTearing, // Force IMMEDIATE
		UnlockedNoTearing // Force MAILBOX
	};

	class WSI
	{
	public:
		WSI();
		void SetPlatform(WSIPlatform* platform);
		void SetPresentMode(PresentMode mode);
		void SetBackbufferSrgb(bool enable);
		void SetSupportPrerotate(bool enable);
		VkSurfaceTransformFlagBitsKHR GetCurrentPrerotate() const;

		PresentMode GetPresentMode() const
		{
			return present_mode;
		}

		bool GetBackbufferSrgb() const
		{
			return srgb_backbuffer_enable;
		}

		bool Init(unsigned num_thread_indices);
		bool InitExternalContext(ContextHandle context);
		bool InitExternalSwapchain(std::vector<Vulkan::ImageHandle> external_images);
		void DeinitExternal();

		~WSI();

		inline Context& GetContext()
		{
			return *context;
		}

		inline Device& GetDevice()
		{
			return *device;
		}

		bool BeginFrame();
		bool EndFrame();
		void SetExternalFrame(unsigned index, Vulkan::Semaphore acquire_semaphore, double frame_time);
		Vulkan::Semaphore ConsumeExternalReleaseSemaphore();

		WSIPlatform& GetPlatform()
		{
			VK_ASSERT(platform);
			return *platform;
		}

		void DeinitSurfaceAndSwapchain();
		void InitSurfaceAndSwapchain(VkSurfaceKHR new_surface);

		float GetEstimatedVideoLatency();
		void SetWindowTitle(const std::string& title);

		double GetSmoothFrameTime() const;
		double GetSmoothElapsedTime() const;

		double GetEstimatedRefreshInterval() const;

		WSITiming& GetTiming()
		{
			return timing;
		}

		static void BuildPrerotateMatrix2x2(VkSurfaceTransformFlagBitsKHR pre_rotate, float mat[4]);

	private:
		void UpdateFramebuffer(unsigned width, unsigned height);

		std::unique_ptr<Context> context;
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		std::vector<VkImage> swapchain_images;
		std::vector<Semaphore> release_semaphores;
		std::unique_ptr<Device> device;
		const VolkDeviceTable* table = nullptr;

		unsigned swapchain_width = 0;
		unsigned swapchain_height = 0;
		float swapchain_aspect_ratio = 1.0f;
		VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
		PresentMode current_present_mode = PresentMode::SyncToVBlank;
		PresentMode present_mode = PresentMode::SyncToVBlank;

		enum class SwapchainError
		{
			None,
			NoSurface,
			Error
		};
		SwapchainError Init_Swapchain(unsigned width, unsigned height);
		bool BlockingInitSwapchain(unsigned width, unsigned height);

		uint32_t swapchain_index = 0;
		bool has_acquired_swapchain_index = false;

		WSIPlatform* platform = nullptr;

		std::vector<Vulkan::ImageHandle> external_swapchain_images;

		unsigned external_frame_index = 0;
		Vulkan::Semaphore external_acquire;
		Vulkan::Semaphore external_release;
		bool frame_is_external = false;
		bool using_display_timing = false;
		bool srgb_backbuffer_enable = true;
		bool current_srgb_backbuffer_enable = true;
		bool support_prerotate = false;
		VkSurfaceTransformFlagBitsKHR swapchain_current_prerotate = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

		bool BeginFrameExternal();
		double external_frame_time = 0.0;

		double smooth_frame_time = 0.0;
		double smooth_elapsed_time = 0.0;

		WSITiming timing;

		void TearDownSwapchain();
		void DrainSwapchain();
	};
}
