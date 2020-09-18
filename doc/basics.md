# Tutorial: The Basics
Think of this documentation as a series explaination documents, describing how to use various parts
of QuantumVk, from [building and integrating](building.md) it into your own project, to setting up the [WSI](wsi.md).
For more tutorial-y style posts, as well as articles about general design, go to the [QuantumGfx website](https://quantumgfx.github.io/) (still being built).

## Initializing Vulkan
Before any vulkan code can be run, the vulkan library must first be loaded into QuantumVk. This is done using [volk](https://github.com/zeux/volk), a vulkan
meta-loader library that dynamically loads the vulkan function pointers from libvulkan.so/libvulkan-1.dll/etc depending on your platform. This is actually 
significantly faster than just loading the dll object library at compile time, and also allows QuantumVk to bootstrap itself in straight from vkGetInstanceProcAddr
if you are already loading vulkan dynamically (for example GLFW automatically loads the vulkan library). Here's how it looks in code:

```c++
#include <quantumvk/quantumvk.hpp>
#include <iostream>

int main()
{
	if(!Vulkan::Context::InitLoader(nullptr))
	{
		std::cout << "Failed to load vulkan dynamic library" << std::endl;
	}
}
```
Note: Most Init functions return false if they fail and true otherwise.

## Creating the Context and the Device
Basic vulkan functionality and instance/gpu initialization is provided by the context object. It essentially encapulates the...
- VkInstance
- VkPhysicalDevice
- Any VkQueues
- VkDevice

but doesn't provide much true functionality. The interface with these objects is done with the device. The context instead
just initializes the objects and allows for basic querying of feature and extension support. Usually these objects are 
managed by the WSI, but for headless examples you manipulate the raw objects. 

```c++
#include <quantumvk/quantumvk.hpp>
#include <iostream>

int main()
{
	if(!Vulkan::Context::InitLoader(nullptr))
	{
		std::cout << "Failed to load vulkan dynamic library" << std::endl;
	}
		
	Vulkan::Context context;
	// Here you can pass additional extensions instead of nullptr
	if(!context.InitInstanceAndDevice(nullptr, 0, nullptr, 0))
	{
		std::cout << "Failed to initialize vulkan context" << std::endl;
	}
	
	Vulkan::Device device;
	// The nullptr and 0 refer to the data in the pipeline cache, which can be stored between applications runs, 
	// and then used to create the pipeline cache, speeding up pipeline creation.
	if(!device.SetContext(&context, nullptr, 0))
	{
		std::cout << "Failed to create device from context" << std::endl;
	}
	
	// Everything is cleaned up via RAII
	
}
```

## Frame Contexts
All work in QuantumVk is seperated into frame contexts. They can map one-to-one with swapchain images, but they don't have to.
Starting a frame context, and looping through them is as simple as...

```c++
int main()
{
	// Initialization...
	
	// This is done automatically for us in Device::SetContext().
	// The default for desktop is 2 frame contexts, and 3 frame contexts on Android
	// (since TBDR renderers typically require a bit more buffering for optimal performance).
	// A frame context generally maps to an on-screen frame, but it does not have to.
	device.InitFrameContexts(2);
	
	// We start in frame context #0.
	// Each frame context has some state associated with it.
	// - Command pools are tied to a frame context.
	// - Queued up command buffers for submission.
	// - Objects which are pending to be destroyed.

	// Let's pretend we're doing this in the first frame.
	{
		// Command buffers are transient in QuantumVk.
		// Once you request a command buffer you must submit it in the current frame context before moving to the next one.
		// More detailed examples of command buffers will follow in future samples.
		// There are different command buffer types which correspond to general purpose queue, async compute, DMA queue, etc.
		// Generic is the default, and the argument can be omitted.
		Vulkan::CommandBufferHandle cmd = device.RequestCommandBuffer(Vulkan::CommandBuffer::Type::Generic);
		
		// Do work...
		
		/ Submitting a command buffer simply queues it up. We will not call vkQueueSubmit and flush out all pending command buffers here unless:
		// - We need to signal a fence.
		// - We need to signal a semaphore.
		Vulkan::Fence *fence = nullptr;
		const unsigned num_semaphores = 0;
		Vulkan::Semaphore *semaphores = nullptr;

		// Command buffers must be submitted. Failure to do so will trip assertions in debug builds.
		device.Submit(cmd, fence, num_semaphores, semaphores);
	}
	
	// Normally, if using the WSI module in QuantumVk (to be introduced later), we don't need to iterate this ourselves since
	// this is called automatically on "QueuePresent". However, for headless operation like this,
	// we need to call this ourselves to mark when we have submitted enough work for the GPU.
	// If we have some pending work in the current frame context, this is flushed out.
	// Fences are signalled internally to keep track of all work that happened in this frame context.
	device->NextFrameContext();
	
	// Now we're in frame context #1, and when starting a frame context, we need to wait for all pending fences associated with the context.
	{
		Vulkan::CommandBufferHandle cmd = device.RequestCommandBuffer(Vulkan::CommandBuffer::Type::Generic);
		
		// More work
		
		device.Submit(cmd);
	}
	
	// Now we're back again in frame context #0, and any resources we used back in the first frame have now been reclaimed,
	// Command pools have been reset and we can reuse the old command buffers.
	// since we have waited for all command buffers which were ever submitted in that old frame context.
	// This is how we get double-buffering between CPU and GPU basically.
	device.NextFrameContext();

	// This is the gist of QuantumVk's lifetime handling. It defers deallocations until we know that any possible work is complete.
	// This is sub-optimal, but it is also 100% deterministic. This I believe is the right abstraction level for a "mid-level" implementation.
	// If you have one very long frame that is doing a lot of work and you're allocating and freeing memory a lot, you might end up with an OOM scenario.
	// To reclaim memory you must call Device::NextFrameContext, or Device::WaitIdle, which also immediately reclaims all memory and frees all pending resources.
	// Since we are resetting all command pools in WaitIdle, all command buffers must have been submitted before calling this, similar to NextFrameContext().
	device.WaitIdle();
}

```

## Resource Creation

The two primary resources in QuantumVk are images and buffers. Resources are just memory organized a specific way, that can be accessed and used by the gpu.
Heres an example of how one might create a resource.

```c++
static Vulkan::BufferHandle CreateBuffer(Vulkan::Device& device)
{
	Vulkan::BufferCreateInfo info;

	// Size in bytes.
	info.size = 64;

	// The domain is where we want the buffer to live.
	// This abstracts the memory type jungle.
	// - Device is DEVICE_LOCAL. Use this for static buffers which are read from many times.
	// - Host is HOST_VISIBLE, but probably not CACHED. Use this for uploads.
	// - CachedHost is HOST_VISIBLE with CACHED. Used for readbacks.
	// - LinkedDeviceHost is a special one which is DEVICE_LOCAL and HOST_VISIBLE.
	//   This matches AMD's pinned 256 MB memory type. Not really used at the moment.
	info.domain = Vulkan::BufferDomain::Device;

	// Usage flags is as you expect. If initial copies are desired as well,
	// the backend will add in transfer usage flags as required.
	info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	// Misc creation flags which don't exist in Vulkan. It's possible to request the buffer to 
	// be cleared to zero on creation for example. For Device-only types, this means allocating
    // a command buffer and submitting that. Barriers are taken care of automatically.
	info.misc = 0;

	// Initial data can be passed in. The data is copied on the transfer queue and barriers are 
	// taken care of. For more control, you can pass in nullptr here and deal with it manually.
	// If you're creating a lot of buffers with initial data in one go, it might makes sense to
    // do the upload manually.
	const void *initial_data = nullptr;

	// Memory is allocated automatically.
	Vulkan::BufferHandle buffer = device.CreateBuffer(info, initial_data);
	return buffer;
}

static Vulkan::ImageHandle CreateImage(Vulkan::Device& device)
{
	// Immutable2dImage sets up a create info struct which matches what we want.
	Vulkan::ImageCreateInfo info = Vulkan::ImageCreateInfo::Immutable2dImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM);

	// We can use an initial layout here. If != UNDEFINED, we need to submit a command buffer with
	// the image barriers to transfer the image to our desired layout.
	// Mostly useful for read-only images which we only touch once from a synchronization point-of-view.
	info.initial_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// Levels == 0 -> automatically deduce it.
	info.levels = 0;

	// We can request mips to be generated automatically.
	// In this case, we only upload the first mip level.
	// Other mips can be optionally manually uploaded via the
	// next_mip member of the ImageInitialData struct.
	info.misc = Vulkan::IMAGE_MISC_GENERATE_MIPS_BIT;

	Vulkan::ImageInitialData initial_data = {};
	static const uint32_t checkerboard[] = {
		0u, ~0u, 0u, ~0u,
		~0u, 0u, ~0u, 0u,
		0u, ~0u, 0u, ~0u,
		~0u, 0u, ~0u, 0u,
	};
	initial_data.data = checkerboard;

	// Memory is allocated automatically.
	Vulkan::ImageHandle handle = device.CreateImage(info, &initial_data);
	return handle;
}
```

Buffers (excepting BufferDomain::Device) and (some types of) images can also be mapped to and unmapped from host memory. 
QuantumVk does not track buffers or tie resources to certain frame contexts, so you must be the one to know when it is 
safe to map memory to a buffer or image. What is meant by that is that nothing prevents you from mapping memory to a 
resource that is in use. You must be certain a particular resource is unused by the gpu, or you must syncronize it some other
way (for example through events). It is suggested that you use the built-in Allocate functions in the command buffer class for
small bits of data you have to pass to the gpu. Otherwise, for a buffer or image you map to every frame, you can create a 
ring of resources, tied to each frame context. Cycling through each resource and mapping memory to it as the frame context
is incremented.
