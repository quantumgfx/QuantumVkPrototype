#include "device.hpp"
#include "images/format.hpp"
#include "misc/type_to_string.hpp"

#include "quantumvk/utils/timer.hpp"
#include <algorithm>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef QM_VULKAN_MT
#include "quantumvk/threading/thread_id.hpp"
static unsigned GetThreadIndex()
{
	return Vulkan::GetCurrentThreadIndex();
}
#define LOCK() std::lock_guard<std::mutex> holder__{lock.lock}
#define DRAIN_FRAME_LOCK() \
	std::unique_lock<std::mutex> holder__{lock.lock}; \
	lock.cond.wait(holder__, [&]() { \
		return lock.counter == 0; \
	})
#else
#define LOCK() ((void)0)
#define DRAIN_FRAME_LOCK() VK_ASSERT(lock.counter == 0)
static unsigned GetThreadIndex()
{
	return 0;
}
#endif

using namespace Util;

namespace Vulkan
{
	void Device::FlushFrame(CommandBuffer::Type type)
	{
		if (type == CommandBuffer::Type::AsyncTransfer)
			SyncBufferBlocks();
		SubmitQueue(type, nullptr, 0, nullptr);
	}

	void Device::SyncBufferBlocks()
	{
		if (dma.vbo.empty() && dma.ibo.empty() && dma.ubo.empty())
			return;

		VkBufferUsageFlags usage = 0;

		auto cmd = RequestCommandBufferNolock(GetThreadIndex(), CommandBuffer::Type::AsyncTransfer);

		for (auto& block : dma.vbo)
		{
			VK_ASSERT(block.offset != 0);
			cmd->CopyBuffer(*block.gpu, 0, *block.cpu, 0, block.offset);
			usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		}

		for (auto& block : dma.ibo)
		{
			VK_ASSERT(block.offset != 0);
			cmd->CopyBuffer(*block.gpu, 0, *block.cpu, 0, block.offset);
			usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		}

		for (auto& block : dma.ubo)
		{
			VK_ASSERT(block.offset != 0);
			cmd->CopyBuffer(*block.gpu, 0, *block.cpu, 0, block.offset);
			usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		}

		dma.vbo.clear();
		dma.ibo.clear();
		dma.ubo.clear();

		// Do not flush graphics or compute in this context.
		// We must be able to inject semaphores into all currently enqueued graphics / compute.
		SubmitStaging(cmd, usage, false);
	}

	void Device::EndFrameContext()
	{
		DRAIN_FRAME_LOCK();
		EndFrameNolock();
	}

	void Device::EndFrameNolock()
	{
		UpdateInvalidProgramsNoLock();

		// Make sure we have a fence which covers all submissions in the frame.
		InternalFence fence;

		if (transfer.need_fence || !Frame().transfer_submissions.empty())
		{
			SubmitQueue(CommandBuffer::Type::AsyncTransfer, &fence, 0, nullptr);
			if (fence.fence != VK_NULL_HANDLE)
			{
				Frame().wait_fences.push_back(fence.fence);
				Frame().recycle_fences.push_back(fence.fence);
			}
			transfer.need_fence = false;
		}

		if (graphics.need_fence || !Frame().graphics_submissions.empty())
		{
			SubmitQueue(CommandBuffer::Type::Generic, &fence, 0, nullptr);
			if (fence.fence != VK_NULL_HANDLE)
			{
				Frame().wait_fences.push_back(fence.fence);
				Frame().recycle_fences.push_back(fence.fence);
			}
			graphics.need_fence = false;
		}

		if (compute.need_fence || !Frame().compute_submissions.empty())
		{
			SubmitQueue(CommandBuffer::Type::AsyncCompute, &fence, 0, nullptr);
			if (fence.fence != VK_NULL_HANDLE)
			{
				Frame().wait_fences.push_back(fence.fence);
				Frame().recycle_fences.push_back(fence.fence);
			}
			compute.need_fence = false;
		}
	}

	void Device::FlushFrame()
	{
		LOCK();
		FlushFrameNolock();
	}

	void Device::FlushFrameNolock()
	{
		FlushFrame(CommandBuffer::Type::AsyncTransfer);
		FlushFrame(CommandBuffer::Type::Generic);
		FlushFrame(CommandBuffer::Type::AsyncCompute);
	}

	////////////////////////////////
	//PerFrame Stuff////////////////
	////////////////////////////////

	PerFrame::PerFrame(Device* device_, unsigned frame_index_)
		: device(*device_)
		, frame_index(frame_index_)
		, table(device_->GetDeviceTable())
		, managers(device_->managers)
	{
		graphics_timeline_semaphore = device.graphics.timeline_semaphore;
		compute_timeline_semaphore = device.compute.timeline_semaphore;
		transfer_timeline_semaphore = device.transfer.timeline_semaphore;

		unsigned count = device_->num_thread_indices;
		graphics_cmd_pool.reserve(count);
		compute_cmd_pool.reserve(count);
		transfer_cmd_pool.reserve(count);
		for (unsigned i = 0; i < count; i++)
		{
			graphics_cmd_pool.emplace_back(device_, device_->graphics_queue_family_index);
			compute_cmd_pool.emplace_back(device_, device_->compute_queue_family_index);
			transfer_cmd_pool.emplace_back(device_, device_->transfer_queue_family_index);
		}
	}

#ifdef VULKAN_DEBUG

	template <typename T, typename U>
	static inline bool exists(const T& container, const U& value)
	{
		return std::find(std::begin(container), std::end(container), value) != std::end(container);
	}

#endif

	void Device::ResetFence(VkFence fence, bool observed_wait)
	{
		LOCK();
		ResetFenceNolock(fence, observed_wait);
	}

	void Device::DestroyBuffer(VkBuffer buffer, const DeviceAllocation& allocation)
	{
		LOCK();
		DestroyBufferNolock(buffer, allocation);
	}

	void Device::DestroyProgramNoLock(Program* program)
	{
		Frame().destroyed_programs.push_back(program);
	}

	void Device::DestroyBufferView(VkBufferView view)
	{
		LOCK();
		DestroyBufferViewNolock(view);
	}

	void Device::DestroyEvent(VkEvent event)
	{
		LOCK();
		DestroyEventNolock(event);
	}

	void Device::DestroyFramebuffer(VkFramebuffer framebuffer)
	{
		LOCK();
		DestroyFramebufferNolock(framebuffer);
	}

	void Device::DestroyImage(VkImage image, const DeviceAllocation& allocation)
	{
		LOCK();
		DestroyImageNolock(image, allocation);
	}

	void Device::DestroySemaphore(VkSemaphore semaphore)
	{
		LOCK();
		DestroySemaphoreNolock(semaphore);
	}

	void Device::RecycleSemaphore(VkSemaphore semaphore)
	{
		LOCK();
		RecycleSemaphoreNolock(semaphore);
	}

	void Device::DestroySampler(VkSampler sampler)
	{
		LOCK();
		DestroySamplerNolock(sampler);
	}

	void Device::DestroyImageView(VkImageView view)
	{
		LOCK();
		DestroyImageViewNolock(view);
	}

	void Device::DestroyImageViewNolock(VkImageView view)
	{
		VK_ASSERT(!exists(Frame().destroyed_image_views, view));
		Frame().destroyed_image_views.push_back(view);
	}

	void Device::DestroyBufferViewNolock(VkBufferView view)
	{
		VK_ASSERT(!exists(Frame().destroyed_buffer_views, view));
		Frame().destroyed_buffer_views.push_back(view);
	}

	void Device::DestroySemaphoreNolock(VkSemaphore semaphore)
	{
		VK_ASSERT(!exists(Frame().destroyed_semaphores, semaphore));
		Frame().destroyed_semaphores.push_back(semaphore);
	}

	void Device::RecycleSemaphoreNolock(VkSemaphore semaphore)
	{
		VK_ASSERT(!exists(Frame().recycled_semaphores, semaphore));
		Frame().recycled_semaphores.push_back(semaphore);
	}

	void Device::DestroyEventNolock(VkEvent event)
	{
		VK_ASSERT(!exists(Frame().recycled_events, event));
		Frame().recycled_events.push_back(event);
	}

	void Device::ResetFenceNolock(VkFence fence, bool observed_wait)
	{
		if (observed_wait)
		{
			table->vkResetFences(device, 1, &fence);
			managers.fence.RecycleFence(fence);
		}
		else
			Frame().recycle_fences.push_back(fence);
	}

	void Device::DestroyImageNolock(VkImage image, const DeviceAllocation& allocation)
	{
		//VK_ASSERT(!exists(Frame().destroyed_images, std::make_pair(image, allocation)));
		Frame().destroyed_images.push_back(std::make_pair(image, allocation));
	}

	void Device::DestroyBufferNolock(VkBuffer buffer, const DeviceAllocation& allocation)
	{
		//VK_ASSERT(!exists(Frame().destroyed_buffers, std::make_pair(buffer, allocation)));
		Frame().destroyed_buffers.push_back(std::make_pair(buffer, allocation));
	}

	void Device::DestroySamplerNolock(VkSampler sampler)
	{
		VK_ASSERT(!exists(Frame().destroyed_samplers, sampler));
		Frame().destroyed_samplers.push_back(sampler);
	}

	void Device::DestroyFramebufferNolock(VkFramebuffer framebuffer)
	{
		VK_ASSERT(!exists(Frame().destroyed_framebuffers, framebuffer));
		Frame().destroyed_framebuffers.push_back(framebuffer);
	}

	void PerFrame::Begin()
	{
		VkDevice vkdevice = device.GetDevice();

		if (device.GetDeviceExtensions().timeline_semaphore_features.timelineSemaphore && graphics_timeline_semaphore && compute_timeline_semaphore && transfer_timeline_semaphore)
		{
			VkSemaphoreWaitInfoKHR info = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR };
			const VkSemaphore semaphores[3] = { graphics_timeline_semaphore, compute_timeline_semaphore, transfer_timeline_semaphore };
			const uint64_t values[3] = { timeline_fence_graphics, timeline_fence_compute, timeline_fence_transfer };

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			if (device.GetDeviceExtensions().timeline_semaphore_features.timelineSemaphore)
			{
				QM_LOG_INFO("Waiting for graphics (%p) %u\n",
					reinterpret_cast<void*>(graphics_timeline_semaphore),
					unsigned(timeline_fence_graphics));
				QM_LOG_INFO("Waiting for compute (%p) %u\n",
					reinterpret_cast<void*>(compute_timeline_semaphore),
					unsigned(timeline_fence_compute));
				QM_LOG_INFO("Waiting for transfer (%p) %u\n",
					reinterpret_cast<void*>(transfer_timeline_semaphore),
					unsigned(timeline_fence_transfer));
			}
#endif

			info.pSemaphores = semaphores;
			info.pValues = values;
			info.semaphoreCount = 3;
			table.vkWaitSemaphoresKHR(vkdevice, &info, UINT64_MAX);
		}

		// If we're using timeline semaphores, these paths should never be hit.
		if (!wait_fences.empty())
		{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			for (auto& fence : wait_fences)
				QM_LOG_INFO("Waiting for Fence: %llx\n", reinterpret_cast<unsigned long long>(fence));
#endif
			table.vkWaitForFences(vkdevice, wait_fences.size(), wait_fences.data(), VK_TRUE, UINT64_MAX);
			wait_fences.clear();
		}

		// If we're using timeline semaphores, these paths should never be hit.
		if (!recycle_fences.empty())
		{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			for (auto& fence : recycle_fences)
				QM_LOG_INFO("Recycling Fence: %llx\n", reinterpret_cast<unsigned long long>(fence));
#endif
			table.vkResetFences(vkdevice, recycle_fences.size(), recycle_fences.data());
			for (auto& fence : recycle_fences)
				managers.fence.RecycleFence(fence);
			recycle_fences.clear();
		}

		for (auto& pool : graphics_cmd_pool)
			pool.Begin();
		for (auto& pool : compute_cmd_pool)
			pool.Begin();
		for (auto& pool : transfer_cmd_pool)
			pool.Begin();

		for (auto& framebuffer : destroyed_framebuffers)
			table.vkDestroyFramebuffer(vkdevice, framebuffer, nullptr);
		for (auto& sampler : destroyed_samplers)
			table.vkDestroySampler(vkdevice, sampler, nullptr);
		for (auto& view : destroyed_image_views)
			table.vkDestroyImageView(vkdevice, view, nullptr);
		for (auto& view : destroyed_buffer_views)
			table.vkDestroyBufferView(vkdevice, view, nullptr);
		for (auto& image : destroyed_images)
			device.managers.memory.FreeImage(image.first, image.second);
		for (auto& buffer : destroyed_buffers)
			device.managers.memory.FreeBuffer(buffer.first, buffer.second);
		for (auto& semaphore : destroyed_semaphores)
			table.vkDestroySemaphore(vkdevice, semaphore, nullptr);
		for (auto& semaphore : recycled_semaphores)
		{
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Recycling semaphore: %llx\n", reinterpret_cast<unsigned long long>(semaphore));
#endif
			managers.semaphore.RecycleSemaphore(semaphore);
		}
		for (auto& event : recycled_events)
			managers.event.RecycleEvent(event);

		{
			for (auto& program : destroyed_programs)
				device.handle_pool.programs.free(program);
		}

		{
			for (auto& shader : destroyed_shaders)
				device.handle_pool.shaders.free(shader);
		}

		for (auto& block : vbo_blocks)
			managers.vbo.RecycleBlock(std::move(block));
		for (auto& block : ibo_blocks)
			managers.ibo.RecycleBlock(std::move(block));
		for (auto& block : ubo_blocks)
			managers.ubo.RecycleBlock(std::move(block));
		for (auto& block : staging_blocks)
			managers.staging.RecycleBlock(std::move(block));

		vbo_blocks.clear();
		ibo_blocks.clear();
		ubo_blocks.clear();
		staging_blocks.clear();

		destroyed_framebuffers.clear();
		destroyed_samplers.clear();
		destroyed_image_views.clear();
		destroyed_buffer_views.clear();
		destroyed_images.clear();
		destroyed_buffers.clear();
		destroyed_semaphores.clear();
		recycled_semaphores.clear();
		recycled_events.clear();

		destroyed_shaders.clear();
		destroyed_programs.clear();

		//int64_t min_timestamp_us = std::numeric_limits<int64_t>::max();
		//int64_t max_timestamp_us = 0;
	}

	PerFrame::~PerFrame()
	{
		Begin();
	}


	void Device::ClearWaitSemaphores()
	{
		for (auto& sem : graphics.wait_semaphores)
			table->vkDestroySemaphore(device, sem->Consume(), nullptr);
		for (auto& sem : compute.wait_semaphores)
			table->vkDestroySemaphore(device, sem->Consume(), nullptr);
		for (auto& sem : transfer.wait_semaphores)
			table->vkDestroySemaphore(device, sem->Consume(), nullptr);

		graphics.wait_semaphores.clear();
		graphics.wait_stages.clear();
		compute.wait_semaphores.clear();
		compute.wait_stages.clear();
		transfer.wait_semaphores.clear();
		transfer.wait_stages.clear();
	}

	void Device::WaitIdle()
	{
		DRAIN_FRAME_LOCK();
		WaitIdleNolock();
	}

	void Device::WaitIdleNolock()
	{
		if (!per_frame.empty())
			EndFrameNolock();

		if (device != VK_NULL_HANDLE)
		{
			if (queue_lock_callback)
				queue_lock_callback();
			auto result = table->vkDeviceWaitIdle(device);
			if (result != VK_SUCCESS)
				QM_LOG_ERROR("vkDeviceWaitIdle failed with code: %d\n", result);
			if (queue_unlock_callback)
				queue_unlock_callback();
		}

		ClearWaitSemaphores();

		// Free memory for buffer pools.
		managers.vbo.Reset();
		managers.ubo.Reset();
		managers.ibo.Reset();
		managers.staging.Reset();
		for (auto& frame : per_frame)
		{
			frame->vbo_blocks.clear();
			frame->ibo_blocks.clear();
			frame->ubo_blocks.clear();
			frame->staging_blocks.clear();
		}

		framebuffer_allocator.Clear();
		transient_allocator.Clear();
		physical_allocator.Clear();

		{
#ifdef QM_VULKAN_MT
			std::lock_guard holder_{ lock.program_lock };
#endif

			for (auto& program : active_programs)
				if(program)
					program->Clear();
		}

		for (auto& frame : per_frame)
		{
			// We have done WaitIdle, no need to wait for extra fences, it's also not safe.
			frame->wait_fences.clear();
			frame->Begin();
		}
	}

	void Device::NextFrameContext()
	{
		DRAIN_FRAME_LOCK();

		// Flush the frame here as we might have pending staging command buffers from init stage.
		EndFrameNolock();

		framebuffer_allocator.BeginFrame();
		transient_allocator.BeginFrame();
		physical_allocator.BeginFrame();

		{
#ifdef QM_VULKAN_MT
			std::lock_guard holder_{ lock.program_lock };
#endif

			for (auto& program : active_programs)
				if (program)
					program->BeginFrame();
		}

		VK_ASSERT(!per_frame.empty());

		frame_context_index++;
		if (frame_context_index >= per_frame.size())
			frame_context_index = 0;

		Frame().Begin();
	}

	void Device::AddFrameCounterNolock()
	{
		lock.counter++;
	}

	void Device::DecrementFrameCounterNolock()
	{
		VK_ASSERT(lock.counter > 0);
		lock.counter--;
#ifdef QM_VULKAN_MT
		lock.cond.notify_one();
#endif
	}
}