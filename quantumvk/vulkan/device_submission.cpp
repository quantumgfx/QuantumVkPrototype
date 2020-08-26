#include "device.hpp"
#include "quantumvk/utils/bitops.hpp"
#include "quantumvk/utils/hash.hpp"
#include "quantumvk/utils/small_vector.hpp"

#ifdef QM_VULKAN_MT
#include "quantumvk/threading/thread_id.hpp"
static unsigned get_thread_index()
{
	return Vulkan::get_current_thread_index();
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
static unsigned get_thread_index()
{
	return 0;
}
#endif

namespace Vulkan
{

	CommandBufferHandle Device::RequestCommandBuffer(CommandBuffer::Type type)
	{
		return RequestCommandBufferForThread(get_thread_index(), type);
	}

	CommandBufferHandle Device::RequestCommandBufferForThread(unsigned thread_index, CommandBuffer::Type type)
	{
		LOCK();
		return RequestCommandBufferNolock(thread_index, type);
	}

	CommandBufferHandle Device::RequestCommandBufferNolock(unsigned thread_index, CommandBuffer::Type type)
	{
#ifndef QM_VULKAN_MT
		VK_ASSERT(thread_index == 0);
#endif
		auto cmd = GetCommandPool(type, thread_index).RequestCommandBuffer();

		VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		table->vkBeginCommandBuffer(cmd, &info);
		AddFrameCounterNolock();
		CommandBufferHandle handle(handle_pool.command_buffers.allocate(this, cmd, pipeline_cache, type));
		handle->SetThreadIndex(thread_index);

		return handle;
	}

	void Device::SubmitSecondary(CommandBuffer& primary, CommandBuffer& secondary)
	{
		{
			LOCK();
			secondary.End();
			DecrementFrameCounterNolock();

#ifdef VULKAN_DEBUG
			auto& pool = GetCommandPool(secondary.GetCommandBufferType(),
				secondary.GetThreadIndex());
			pool.SignalSubmitted(secondary.GetCommandBuffer());
#endif
		}

		VkCommandBuffer secondary_cmd = secondary.GetCommandBuffer();
		table->vkCmdExecuteCommands(primary.GetCommandBuffer(), 1, &secondary_cmd);
	}

	CommandBufferHandle Device::RequestSecondaryCommandBufferForThread(unsigned thread_index, const Framebuffer* framebuffer, unsigned subpass, CommandBuffer::Type type)
	{
		LOCK();

		auto cmd = GetCommandPool(type, thread_index).RequestSecondaryCommandBuffer();
		VkCommandBufferBeginInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		VkCommandBufferInheritanceInfo inherit = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };

		inherit.framebuffer = VK_NULL_HANDLE;
		inherit.renderPass = framebuffer->GetCompatibleRenderPass().GetRenderPass();
		inherit.subpass = subpass;
		info.pInheritanceInfo = &inherit;
		info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

		table->vkBeginCommandBuffer(cmd, &info);
		AddFrameCounterNolock();
		CommandBufferHandle handle(handle_pool.command_buffers.allocate(this, cmd, pipeline_cache, type));
		handle->SetThreadIndex(thread_index);
		handle->SetIsSecondary();
		return handle;
	}

	QueueData& Device::GetQueueData(CommandBuffer::Type type)
	{
		switch (GetPhysicalQueueType(type))
		{
		default:
		case CommandBuffer::Type::Generic:
			return graphics;
		case CommandBuffer::Type::AsyncCompute:
			return compute;
		case CommandBuffer::Type::AsyncTransfer:
			return transfer;
		}
	}

	VkQueue Device::GetVkQueue(CommandBuffer::Type type) const
	{
		switch (GetPhysicalQueueType(type))
		{
		default:
		case CommandBuffer::Type::Generic:
			return graphics_queue;
		case CommandBuffer::Type::AsyncCompute:
			return compute_queue;
		case CommandBuffer::Type::AsyncTransfer:
			return transfer_queue;
		}
	}

	CommandPool& Device::GetCommandPool(CommandBuffer::Type type, unsigned thread)
	{
		switch (GetPhysicalQueueType(type))
		{
		default:
		case CommandBuffer::Type::Generic:
			return Frame().graphics_cmd_pool[thread];
		case CommandBuffer::Type::AsyncCompute:
			return Frame().compute_cmd_pool[thread];
		case CommandBuffer::Type::AsyncTransfer:
			return Frame().transfer_cmd_pool[thread];
		}
	}

	Util::SmallVector<CommandBufferHandle>& Device::GetQueueSubmission(CommandBuffer::Type type)
	{
		switch (GetPhysicalQueueType(type))
		{
		default:
		case CommandBuffer::Type::Generic:
			return Frame().graphics_submissions;
		case CommandBuffer::Type::AsyncCompute:
			return Frame().compute_submissions;
		case CommandBuffer::Type::AsyncTransfer:
			return Frame().transfer_submissions;
		}
	}

	void Device::Submit(CommandBufferHandle& cmd, Fence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		// Lock mutex
		LOCK();
		SubmitNolock(std::move(cmd), fence, semaphore_count, semaphores);
	}

	CommandBuffer::Type Device::GetPhysicalQueueType(CommandBuffer::Type queue_type) const
	{
		// This correction only applies to async graphics
		if (queue_type != CommandBuffer::Type::AsyncGraphics)
		{
			return queue_type;
		}
		else
		{
			if (graphics_queue_family_index == compute_queue_family_index && graphics_queue != compute_queue)
				return CommandBuffer::Type::AsyncCompute; // If the graphics and compute queue families match, but the queues don't, run this command cocurrently on the compute queue.
			else
				return CommandBuffer::Type::Generic;
		}
	}

	void Device::SubmitNolock(CommandBufferHandle cmd, Fence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		//Get the command buffer type
		auto type = cmd->GetCommandBufferType();
		auto& submissions = GetQueueSubmission(type);
#ifdef VULKAN_DEBUG
		auto& pool = GetCommandPool(type, cmd->GetThreadIndex());
		pool.SignalSubmitted(cmd->GetCommandBuffer());
#endif

		//End command buffer
		cmd->End();
		submissions.push_back(std::move(cmd));

		InternalFence signalled_fence;

		if (fence || semaphore_count)
		{
			SubmitQueue(type, fence ? &signalled_fence : nullptr, semaphore_count, semaphores);
		}

		if (fence)
		{
			VK_ASSERT(!*fence);
			if (signalled_fence.value)
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.value, signalled_fence.timeline));
			else
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.fence));
		}

		DecrementFrameCounterNolock();
	}

	void Device::SubmitEmpty(CommandBuffer::Type type, Fence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		LOCK();
		SubmitEmptyNolock(type, fence, semaphore_count, semaphores);
	}

	void Device::SubmitEmptyNolock(CommandBuffer::Type type, Fence* fence,
		unsigned semaphore_count, Semaphore* semaphores)
	{
		if (type != CommandBuffer::Type::AsyncTransfer)
			FlushFrame(CommandBuffer::Type::AsyncTransfer);

		InternalFence signalled_fence;
		SubmitQueue(type, fence ? &signalled_fence : nullptr, semaphore_count, semaphores);
		if (fence)
		{
			if (signalled_fence.value)
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.value, signalled_fence.timeline));
			else
				*fence = Fence(handle_pool.fences.allocate(this, signalled_fence.fence));
		}
	}

	void Device::SubmitEmptyInner(CommandBuffer::Type type, InternalFence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		auto& data = GetQueueData(type);
		VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
		VkTimelineSemaphoreSubmitInfoKHR timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

		if (ext->timeline_semaphore_features.timelineSemaphore)
			submit.pNext = &timeline_info;

		VkSemaphore timeline_semaphore = data.timeline_semaphore;
		uint64_t timeline_value = ++data.current_timeline;

		VkQueue queue = GetVkQueue(type);
		switch (type)
		{
		default:
		case CommandBuffer::Type::Generic:
			Frame().timeline_fence_graphics = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			if (ext->timeline_semaphore_features.timelineSemaphore)
			{
				QM_LOG_INFO("Signal graphics: (%p) %u\n",
					reinterpret_cast<void*>(timeline_semaphore),
					unsigned(data.current_timeline));
			}
#endif
			break;

		case CommandBuffer::Type::AsyncCompute:
			Frame().timeline_fence_compute = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			if (ext->timeline_semaphore_features.timelineSemaphore)
			{
				QM_LOG_INFO("Signal compute: (%p) %u\n",
					reinterpret_cast<void*>(timeline_semaphore),
					unsigned(data.current_timeline));
			}
#endif
			break;

		case CommandBuffer::Type::AsyncTransfer:
			Frame().timeline_fence_transfer = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			if (ext->timeline_semaphore_features.timelineSemaphore)
			{
				QM_LOG_INFO("Signal transfer: (%p) %u\n",
					reinterpret_cast<void*>(timeline_semaphore),
					unsigned(data.current_timeline));
			}
#endif
			break;
		}

		// Add external signal semaphores.
		Util::SmallVector<VkSemaphore> signals;
		if (ext->timeline_semaphore_features.timelineSemaphore)
		{
			// Signal once and distribute the timeline value to all.
			timeline_info.signalSemaphoreValueCount = 1;
			timeline_info.pSignalSemaphoreValues = &timeline_value;
			submit.signalSemaphoreCount = 1;
			submit.pSignalSemaphores = &timeline_semaphore;

			if (fence)
			{
				fence->timeline = timeline_semaphore;
				fence->value = timeline_value;
				fence->fence = VK_NULL_HANDLE;
			}

			for (unsigned i = 0; i < semaphore_count; i++)
			{
				VK_ASSERT(!semaphores[i]);
				semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, timeline_value, timeline_semaphore));
			}
		}
		else
		{
			if (fence)
			{
				fence->timeline = VK_NULL_HANDLE;
				fence->value = 0;
			}

			for (unsigned i = 0; i < semaphore_count; i++)
			{
				VkSemaphore cleared_semaphore = managers.semaphore.RequestClearedSemaphore();
				signals.push_back(cleared_semaphore);
				VK_ASSERT(!semaphores[i]);
				semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, cleared_semaphore, true));
			}

			submit.signalSemaphoreCount = signals.size();
			if (!signals.empty())
				submit.pSignalSemaphores = signals.data();
		}

		// Add external wait semaphores.
		Util::SmallVector<VkSemaphore> waits;
		Util::SmallVector<uint64_t> waits_count;
		auto stages = std::move(data.wait_stages);

		for (auto& semaphore : data.wait_semaphores)
		{
			auto wait = semaphore->Consume();
			if (!semaphore->GetTimelineValue())
			{
				if (semaphore->CanRecycle())
					Frame().recycled_semaphores.push_back(wait);
				else
					Frame().destroyed_semaphores.push_back(wait);
			}
			waits.push_back(wait);
			waits_count.push_back(semaphore->GetTimelineValue());
		}

		data.wait_stages.clear();
		data.wait_semaphores.clear();

		submit.waitSemaphoreCount = waits.size();
		if (!stages.empty())
			submit.pWaitDstStageMask = stages.data();
		if (!waits.empty())
			submit.pWaitSemaphores = waits.data();

		if (!waits_count.empty())
		{
			timeline_info.waitSemaphoreValueCount = waits_count.size();
			timeline_info.pWaitSemaphoreValues = waits_count.data();
		}

		VkFence cleared_fence = fence && !ext->timeline_semaphore_features.timelineSemaphore ? managers.fence.RequestClearedFence() : VK_NULL_HANDLE;
		if (fence)
			fence->fence = cleared_fence;

		if (queue_lock_callback)
			queue_lock_callback();
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		if (cleared_fence)
			QM_LOG_INFO("Signalling Fence: %llx\n", reinterpret_cast<unsigned long long>(cleared_fence));
#endif

		VkResult result = table->vkQueueSubmit(queue, 1, &submit, cleared_fence);
		if (ImplementationQuirks::get().queue_wait_on_submission)
			table->vkQueueWaitIdle(queue);
		if (queue_unlock_callback)
			queue_unlock_callback();

		if (result != VK_SUCCESS)
			QM_LOG_ERROR("vkQueueSubmit failed (code: %d).\n", int(result));

		if (!ext->timeline_semaphore_features.timelineSemaphore)
			data.need_fence = true;

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		const char* queue_name = nullptr;
		switch (type)
		{
		default:
		case CommandBuffer::Type::Generic:
			queue_name = "Graphics";
			break;
		case CommandBuffer::Type::AsyncCompute:
			queue_name = "Compute";
			break;
		case CommandBuffer::Type::AsyncTransfer:
			queue_name = "Transfer";
			break;
		}

		QM_LOG_INFO("Empty submission to %s queue:\n", queue_name);
		for (uint32_t i = 0; i < submit.waitSemaphoreCount; i++)
		{
			//QM_LOG_INFO("  Waiting for semaphore: %llx in stages %s\n",
				//reinterpret_cast<unsigned long long>(submit.pWaitSemaphores[i]),
				//stage_flags_to_string(submit.pWaitDstStageMask[i]).c_str());
		}

		for (uint32_t i = 0; i < submit.signalSemaphoreCount; i++)
		{
			QM_LOG_INFO("  Signalling semaphore: %llx\n",
				reinterpret_cast<unsigned long long>(submit.pSignalSemaphores[i]));
		}
#endif
	}

	void Device::SubmitStaging(CommandBufferHandle& cmd, VkBufferUsageFlags usage, bool flush)
	{
		auto access = BufferUsageToPossibleAccess(usage);
		auto stages = BufferUsageToPossibleStages(usage);
		VkQueue src_queue = GetVkQueue(cmd->GetCommandBufferType());

		if (src_queue == graphics_queue && src_queue == compute_queue)
		{ // There is only one queue. Ensure all writes to the buffer are finished and then submit it normally.
			// For single-queue systems, just use a pipeline barrier.
			cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, stages, access);
			SubmitNolock(cmd, nullptr, 0, nullptr);
		}
		else
		{
			auto compute_stages = stages & (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);

			auto compute_access = access & (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

			auto graphics_stages = stages;

			if (src_queue == graphics_queue)
			{
				// Make sure all writes are finished and visible
				cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, graphics_stages, access);

				if (compute_stages != 0)
				{
					// Submit is and make sure all graphics submissions are finished before another AsyncCompute submit
					Semaphore sem;
					SubmitNolock(cmd, nullptr, 1, &sem);
					AddWaitSemaphoreNolock(CommandBuffer::Type::AsyncCompute, sem, compute_stages, flush);
				}
				else // Just submit. All other uses of the resources will be on the same queue
					SubmitNolock(cmd, nullptr, 0, nullptr);
			}
			else if (src_queue == compute_queue)
			{
				// Make sure all writes are finished and visible
				cmd->Barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, compute_stages, compute_access);

				if (graphics_stages != 0)
				{
					Semaphore sem;
					SubmitNolock(cmd, nullptr, 1, &sem);
					AddWaitSemaphoreNolock(CommandBuffer::Type::Generic, sem, graphics_stages, flush);
				}
				else
					SubmitNolock(cmd, nullptr, 0, nullptr);
			}
			else
			{
				//This is running on the transfer queue. No need for a barrier as smeaphores will take care of it
				if (graphics_stages != 0 && compute_stages != 0)
				{
					Semaphore semaphores[2];
					SubmitNolock(cmd, nullptr, 2, semaphores);
					//Graphics and compute submission wait for this result
					AddWaitSemaphoreNolock(CommandBuffer::Type::Generic, semaphores[0], graphics_stages, flush);
					AddWaitSemaphoreNolock(CommandBuffer::Type::AsyncCompute, semaphores[1], compute_stages, flush);
				}
				else if (graphics_stages != 0)
				{
					Semaphore sem;
					SubmitNolock(cmd, nullptr, 1, &sem);
					//Generic submissions wait for this result
					AddWaitSemaphoreNolock(CommandBuffer::Type::Generic, sem, graphics_stages, flush);
				}
				else if (compute_stages != 0)
				{
					Semaphore sem;
					SubmitNolock(cmd, nullptr, 1, &sem);
					//Compute submissions wait for this result
					AddWaitSemaphoreNolock(CommandBuffer::Type::AsyncCompute, sem, compute_stages, flush);
				}
				else
					//Just submit
					SubmitNolock(cmd, nullptr, 0, nullptr);
			}
		}
	}

	void Device::SubmitQueue(CommandBuffer::Type type, InternalFence* fence, unsigned semaphore_count, Semaphore* semaphores)
	{
		//Get queue type
		type = GetPhysicalQueueType(type);

		// Always check if we need to flush pending transfers.
		if (type != CommandBuffer::Type::AsyncTransfer)
			FlushFrame(CommandBuffer::Type::AsyncTransfer);

		auto& data = GetQueueData(type);
		auto& submissions = GetQueueSubmission(type);

		if (submissions.empty())
		{
			//If there are no submissions, but fences/semaphores depend on this submission, then submit an empty command
			if (fence || semaphore_count)
				SubmitEmptyInner(type, fence, semaphore_count, semaphores);
			return;
		}

		VkSemaphore timeline_semaphore = data.timeline_semaphore;
		uint64_t timeline_value = ++data.current_timeline;
		//Get the queue
		VkQueue queue = GetVkQueue(type);
		switch (type)
		{
		default:
		case CommandBuffer::Type::Generic:
			Frame().timeline_fence_graphics = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Signal graphics: (%p) %u\n",
				reinterpret_cast<void*>(timeline_semaphore),
				unsigned(data.current_timeline));
#endif
			break;

		case CommandBuffer::Type::AsyncCompute:
			Frame().timeline_fence_compute = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Signal compute: (%p) %u\n",
				reinterpret_cast<void*>(timeline_semaphore),
				unsigned(data.current_timeline));
#endif
			break;

		case CommandBuffer::Type::AsyncTransfer:
			Frame().timeline_fence_transfer = data.current_timeline;
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
			QM_LOG_INFO("Signal transfer: (%p) %u\n",
				reinterpret_cast<void*>(timeline_semaphore),
				unsigned(data.current_timeline));
#endif
			break;
		}

		//TODO persistant memory (aka just a vector in device class)

		//Commands to submit
		Util::SmallVector<VkCommandBuffer> cmds;
		cmds.reserve(submissions.size());

		//Batched queue submits
		Util::SmallVector<VkSubmitInfo> submits;
		Util::SmallVector<VkTimelineSemaphoreSubmitInfoKHR> timeline_infos;

		submits.reserve(2);
		timeline_infos.reserve(2);

		size_t last_cmd = 0;

		Util::SmallVector<VkSemaphore> waits[2];
		Util::SmallVector<uint64_t> wait_counts[2];
		Util::SmallVector<VkFlags> wait_stages[2];
		Util::SmallVector<VkSemaphore> signals[2];
		Util::SmallVector<uint64_t> signal_counts[2];

		// Add external wait semaphores.
		wait_stages[0] = std::move(data.wait_stages);

		for (auto& semaphore : data.wait_semaphores)
		{
			auto wait = semaphore->Consume();
			if (!semaphore->GetTimelineValue())
			{
				if (semaphore->CanRecycle())
					Frame().recycled_semaphores.push_back(wait);
				else
					Frame().destroyed_semaphores.push_back(wait);
			}
			wait_counts[0].push_back(semaphore->GetTimelineValue());
			waits[0].push_back(wait);
		}

		//Reset wait stages and semaphores
		data.wait_stages.clear();
		data.wait_semaphores.clear();

		for (auto& cmd : submissions)
		{
			if (cmd->SwapchainTouched() && !wsi.touched && !wsi.consumed)
			{
				// If cmd involves swapchain
				if (!cmds.empty())
				{
					// If submmission contains some commands that don't involve the swapchain

					// Push them into thier own submission.

					// Create new submission and timeline-semaphore-info
					submits.emplace_back();
					timeline_infos.emplace_back();

					//Set stype
					auto& timeline_info = timeline_infos.back();
					timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

					auto& submit = submits.back();
					submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

					//If timeline semaphores supported, set pnext
					if (ext->timeline_semaphore_features.timelineSemaphore)
						submit.pNext = &timeline_info;

					// This submission will batch the non-swapchain involving commands together
					submit.commandBufferCount = cmds.size() - last_cmd;
					submit.pCommandBuffers = cmds.data() + last_cmd;

					last_cmd = cmds.size();
				}
				//Indicate that the wsi is involved in this submission
				wsi.touched = true;
			}
			//Push command into pending submission queue
			cmds.push_back(cmd->GetCommandBuffer());
		}

		if (cmds.size() > last_cmd)
		{
			//If there are commands that weren't part of the first submit (which there will always be)

			unsigned index = submits.size();

			// Push all pending cmd buffers to their own submission.
			// Create new submission and timeline-semaphore-info
			submits.emplace_back();
			timeline_infos.emplace_back();

			//Set stype
			auto& timeline_info = timeline_infos.back();
			timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR };

			auto& submit = submits.back();
			submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

			// If timeline semaphores supported, set pnext
			if (ext->timeline_semaphore_features.timelineSemaphore)
				submit.pNext = &timeline_info;

			submit.commandBufferCount = cmds.size() - last_cmd;
			submit.pCommandBuffers = cmds.data() + last_cmd;

			// No need to add QueueData.wait stages/semaphores to this second submission
			// All queueSubmission begin execution in order. They just may complete out of order.

			// If the swapchain is touched and it has an aquire semaphore
			if (wsi.touched && !wsi.consumed)
			{
				static const VkFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				if (wsi.acquire && wsi.acquire->GetSemaphore() != VK_NULL_HANDLE)
				{
					// Then make this submission batch (which has one or more swapchain touching commands buffers) wait for the aquire semaphore.
					// Basically this batch will wait for vkAquireNextImageKHR to complete before being submitted, as it has commands that depend on the
					// swapchain image.
					VK_ASSERT(wsi.acquire->IsSignalled());
					VkSemaphore sem = wsi.acquire->Consume();

					waits[index].push_back(sem);
					wait_counts[index].push_back(wsi.acquire->GetTimelineValue());
					wait_stages[index].push_back(wait);

					if (!wsi.acquire->GetTimelineValue())
					{
						if (wsi.acquire->CanRecycle())
							Frame().recycled_semaphores.push_back(sem);
						else
							Frame().destroyed_semaphores.push_back(sem);
					}

					wsi.acquire.Reset();
				}

				VkSemaphore release = managers.semaphore.RequestClearedSemaphore();
				wsi.release = Semaphore(handle_pool.semaphores.allocate(this, release, true));
				wsi.release->SetInternalSyncObject();
				signals[index].push_back(wsi.release->GetSemaphore());
				signal_counts[index].push_back(0);
				wsi.consumed = true;
			}
			last_cmd = cmds.size();
		}

		// In short, the algorithm above puts commands into at most two batches. The first can be submitted and work on it can start immediately.
		// While the second must wait for the aquire semaphore to finish. For example:
		// Key: N - command that doesn't touch the swapchain, S - command that involves the swapchain
		// Batch 1: (N, N, N, N, N) - The first batch doesn't ever use the swapchain, so it doesn't need to wait for it.
		// Batch 2: (S, N, S, S, N, N) - The second involves the swapchain, so it must wait for VkAquireNextImageKHR to finish.

		VkFence cleared_fence = fence && !ext->timeline_semaphore_features.timelineSemaphore ? managers.fence.RequestClearedFence() : VK_NULL_HANDLE;

		if (fence)
			fence->fence = cleared_fence;

		// Add external signal semaphores.
		if (ext->timeline_semaphore_features.timelineSemaphore)
		{
			// Signal once and distribute the timeline value to all.
			signals[submits.size() - 1].push_back(timeline_semaphore);
			signal_counts[submits.size() - 1].push_back(timeline_value);

			if (fence)
			{
				fence->timeline = timeline_semaphore;
				fence->value = timeline_value;
				fence->fence = VK_NULL_HANDLE;
			}

			for (unsigned i = 0; i < semaphore_count; i++)
			{
				VK_ASSERT(!semaphores[i]);
				semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, timeline_value, timeline_semaphore));
			}
		}
		else
		{
			if (fence)
			{
				fence->timeline = VK_NULL_HANDLE;
				fence->value = 0;
			}

			for (unsigned i = 0; i < semaphore_count; i++)
			{
				VkSemaphore cleared_semaphore = managers.semaphore.RequestClearedSemaphore();
				signals[submits.size() - 1].push_back(cleared_semaphore);
				signal_counts[submits.size() - 1].push_back(0);
				VK_ASSERT(!semaphores[i]);
				semaphores[i] = Semaphore(handle_pool.semaphores.allocate(this, cleared_semaphore, true));
			}
		}

		//Gather all infomation for the submits
		for (unsigned i = 0; i < submits.size(); i++)
		{
			auto& submit = submits[i];
			auto& timeline_submit = timeline_infos[i];

			submit.waitSemaphoreCount = waits[i].size();
			submit.pWaitSemaphores = waits[i].data();
			submit.pWaitDstStageMask = wait_stages[i].data();
			timeline_submit.waitSemaphoreValueCount = submit.waitSemaphoreCount;
			timeline_submit.pWaitSemaphoreValues = wait_counts[i].data();

			submit.signalSemaphoreCount = signals[i].size();
			submit.pSignalSemaphores = signals[i].data();
			timeline_submit.signalSemaphoreValueCount = submit.signalSemaphoreCount;
			timeline_submit.pSignalSemaphoreValues = signal_counts[i].data();
		}

		if (queue_lock_callback)
			queue_lock_callback();
#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		if (cleared_fence)
			QM_LOG_ERROR("Signalling fence: %llx\n", reinterpret_cast<unsigned long long>(cleared_fence));
#endif
		//Submit the command batches
		VkResult result = table->vkQueueSubmit(queue, submits.size(), submits.data(), cleared_fence);
		if (ImplementationQuirks::get().queue_wait_on_submission)
			table->vkQueueWaitIdle(queue);
		if (queue_unlock_callback)
			queue_unlock_callback();

		if (result != VK_SUCCESS)
			QM_LOG_ERROR("vkQueueSubmit failed (code: %d).\n", int(result));

		submissions.clear();

		if (!ext->timeline_semaphore_features.timelineSemaphore)
			data.need_fence = true;

#if defined(VULKAN_DEBUG) && defined(SUBMIT_DEBUG)
		const char* queue_name = nullptr;
		switch (type)
		{
		default:
		case CommandBuffer::Type::Generic:
			queue_name = "Graphics";
			break;
		case CommandBuffer::Type::AsyncCompute:
			queue_name = "Compute";
			break;
		case CommandBuffer::Type::AsyncTransfer:
			queue_name = "Transfer";
			break;
		}

		for (auto& submit : submits)
		{
			QM_LOG_INFO("Submission to %s queue:\n", queue_name);
			for (uint32_t i = 0; i < submit.waitSemaphoreCount; i++)
			{
				//QM_LOG_INFO("  Waiting for semaphore: %llx in stages %s\n",
					//reinterpret_cast<unsigned long long>(submit.pWaitSemaphores[i]),
					//StageFlagsToString(submit.pWaitDstStageMask[i]).c_str());
			}

			for (uint32_t i = 0; i < submit.commandBufferCount; i++)
				QM_LOG_INFO(" Command Buffer %llx\n", reinterpret_cast<unsigned long long>(submit.pCommandBuffers[i]));

			for (uint32_t i = 0; i < submit.signalSemaphoreCount; i++)
			{
				QM_LOG_INFO("  Signalling semaphore: %llx\n",
					reinterpret_cast<unsigned long long>(submit.pSignalSemaphores[i]));
			}
		}
#endif
	}

}