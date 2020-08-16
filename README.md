# QuantumVk
QuantumVk is a fast and flexible middle-ware library for Vulkan, written in C++17. It seeks to improve
development, remove boiler-plate and provide a higher-level API for Vulkan, without sacrificing Vulkan's unique advantage: its speed.
QuantumVk takes inspiration from [Granite Engine](https://github.com/Themaister/Granite), created by the Themaister
(specifically Granite's Vulkan backend).

# Building
In order to build QuantumVk, you must have downloaded the following, regardless of platform:
- [Git](https://git-scm.com/downloads): To retrieve the repository.
- [Cmake](https://cmake.org/): QuantumVk uses Cmake as a build system.
- [Python 3](https://www.python.org/downloads/): Glslang (one of QuantumVk's dependencies) requires python to build.
- [Vulkan SDK](https://vulkan.lunarg.com/): The vulkan sdk is used to access vulkan functionality. QuantumVk doesn't link directly to the vulkan library. Instead [volk](https://github.com/zeux/volk) is used to link dynamically to the vulkan .dll/.so/.dylib depending on the platform.

#### Windows
Windows is currently the only tested platform, though Linux, Andriod, Mac OSX and ios support is currently being worked on (Mac and ios via MoltenVk).
- [Visual Studio 2019](https://visualstudio.microsoft.com/vs/): The suggested developement IDE on Windows for QuantumVk.

# Features
QuantumVk provides many helper classes and functions without completely obscuring the Vulkan element. For example you can use the library 
and never call a vk command once, but QuantumVk also allows you to access raw vulkan resources. For example:
```c++
BufferHandle buffer = device->CreateBuffer(my_create_info)

CommandHandle cmd = device->RequestCommandBuffer(CommandBuffer::Type::Generic)

cmd->FillBuffer(*buffer, 0);

device->Submit(cmd);
```
can just as easily be acomplished using 

```c++
BufferHandle buffer = device->CreateBuffer(my_create_info)

CommandHandle cmd = device->RequestCommandBuffer(CommandBuffer::Type::Generic)

vkCmdFillBuffer(cmd->GetCommandBuffer(), 0, 0, VK_WHOLE_SIZE);

device->Submit(cmd);
```

This means that you can let QuantumVk handle all the low level boiler-plate stuff, while you can manipulate commands and resources at a higher-level.
What follows is an incomplete list of QuantumVk's features:

#### Custom Vulkan library loading
QuantumVk can either automaticly load vulkan functions via volk, or the user can specify a custom PFN_vkGetInstanceProcAddress.
```c++
// Attempts to load vulkan from the corresponding shared library for the platform
// Windows: vulkan-1.dll
// Linux: libvulkan.so.1 or libvulkan.so
// Apple: libvulkan.1.dylib
Vulkan::Context::InitLoader(nullptr);

// Bootstraps into glfw's loading functional

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char *name)
{
	return reinterpret_cast<PFN_vkVoidFunction>(glfwGetInstanceProcAddress(instance, name));
}

Vulkan::Context::InitLoader(GetInstanceProcAddr);

```

#### Extension Support
QuantumVk exposes extension functionality to the user. Any innately supported extensions (that it extensions that are either used
by QuantumVk to make it faster, or extensions I find are extremely useful and thus deserve their own API) are enabled automatically
by QuantumVk. QuantumVk will run on any (vulkan) device even if these extensions are unsupported.
A list of innate extensions is as follows...

Layers:
- VK_LAYER_KHRONOS_validation (validation layer, only in debug)
- VK_LAYER_LUNARG_standard_validation (only if no Khronos validation, only in debug)

Instance Extensions:
- VK_KHR_get_physical_device_properties2 (core for Vulkan 1.1 and higher)
- VK_KHR_external_memory_capabilities (core for Vulkan 1.1 and higher. Only enabled if device_props 2 is also supported)
- VK_KHR_external_semaphore_capabilities (core for Vulkan 1.1 and higher. Only enabled if device_props 2 is also supported)
- VK_EXT_debug_utils (only for debugging info)
- VK_KHR_get_surface_capabilities2 (if khr surface is enabled by WSI)
- VK_KHR_debug_report (if VK_EXT_debug_utils not supported, only in debugging)

Device Extensions:


#### Automatic memory management
QuantumVk uses Amd's [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) to create
resources involving memory, such as buffers, images, and attachments. Memory types are abstracted into the BufferDomain and ImageDomain
enum classes.
```c++
enum class BufferDomain
{
	Device, // Device local. Probably not visible from CPU.
	LinkedDeviceHost, // On desktop, directly mapped VRAM over PCI.
	Host, // Host-only, needs to be synced to GPU. Prefer coherent Might be device local as well on iGPUs.
	CachedHost, //Host visible and host cached
};

enum class ImageDomain
{
	Physical, // Device local
	Transient, // Not backed by real memory, used for transient attachments
	LinearHostCached, // Visible on host as linear stream of pixels (preferes to be cached)
	LinearHost // Visible on host as linear stream of pixels (preferes to be coherent)
};
```

Furthermore initial staging copies are all directly managed by the library. Simply pass in data and it will either by copied via dma or mapped on the host.
Note: this can be disabled for a particular allocation if the use wants to manage the object themselves.

Buffers and images can be easily too using basic CreateInfo structs
```c++
BufferCreateInfo buffer_create_info{};
buffer_create_info.domain = BufferDomain::CachedHost;
buffer_create_info.size = 10 *  1024;
buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BIT;
buffer_create_info.misc = BUFFER_MISC_ZERO_INITIALIZE_BIT;

BufferHandle buffer = device->CreateBuffer(buffer_create_info);
```

Finnally (un)mapping memory is all automatic and quick. Amost all host visible memory is persistantly mapped, meaning it costs almost nothing
to call MapMemory(). Invalidation and Flushing are automatically taken care of.

```c++
BufferHandle buffer;

// This memory is going to be written to (no need to invalidate)
void* buffer_data = device->MapHostBuffer(*buffer, MEMORY_ACCESS_WRITE_BIT);

void* my_data;

// Copy to my data to buffer
memcpy(buffer_data, my_data, 1024);

// Memory was just written to (so it now must be flushed)
device->UnmapHostBuffer(*buffer, MEMORY_ACCESS_WRITE_BIT)

```

# Design

## Lazy On-Demand Creation
Something QuantumVk does quite often is lazily create objects and resources. Particularly with the render state. It is just very hard and annoying to be
completely explicit 100% of the time. Instead of having you (the user) fill in the entire VkGraphicsPipelineCreateInfo structure, fill out the VkPipelineLayout
explicity call vkCreateGraphicsPipelines, manage some pipeline object, and make sure it isn't in use when you delete it, QuantumVk abstracts this all away via 
lazy creation and hashmaps and caches. QuantumVk instead has you set some basic state info in the command buffer, and either generates a
new renderpass/pipeline or retrieves a previously used resource via hashmaps.

# Current Work
The basic library is now functional. I am currently working on removing previous limitations and adding more features.
My current TODO list includes:

- Add true support for VK_EXT_descriptor_indexing
- Add task and mesh shaders
- Add save/restore state to command buffers
- Multiplatform support (probably Mac OSX via MoltenVk first)
- Add GPU profiling
- Add true support VK_KHR_ray_tracing (raytracing is still in beta, so maybe in some kind of optional implementation?)