# QuantumVk
QuantumVk is a fast and flexible middle-ware library for Vulkan, written in C++17. It seeks to improve
development, remove boiler-plate and provide a higher-level API for Vulkan, without sacrificing Vulkan's unique advantage: its speed.
QuantumVk takes inspiration from [Granite Engine](https://github.com/Themaister/Granite), created by the Themaister
(specifically Granite's Vulkan backend).

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
#### Window System Integration

# Design

## Lazy On-Demand Creation
Something QuantumVk does quite often is lazily create objects and resources. Particularly with the render state. It is just very hard and annoying to be
completely explicit 100% of the time. Instead of having you (the user) fill in the entire VkGraphicsPipelineCreateInfo structure, fill out the VkPipelineLayout
explicity call vkCreateGraphicsPipelines, manage some pipeline object, and make sure it isn't in use when you delete it, QuantumVk abstracts this all away via 
lazy creation and [Hashmaps and Caches](#Hashmaps-and-Caches). QuantumVk instead has you set some basic state info in the command buffer, and either generates a
new renderpass/pipeline or retrieves a previously used resource via hashmaps.

## Hashmaps and Caches


# Dependencies
QuantumVk uses a few libraries internally (they are mostly contained as submodules).

- [Volk](https://github.com/zeux/volk): A meta-loader for Vulkan.
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator): A simple to use memory allocation library for Vulkan.
- [Fossilize](https://github.com/ValveSoftware/Fossilize): Vulkan object serializer, similar to VkPipelineCache but for more objects.
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross): Library for parsing SPIRV and converting it to and from other languages.

# Current Work
The basic library is now functional. I am currently working on removing previous limitations and adding more features.
My current TODO list includes:

- Add descriptor set arrays.
- Add true support for VK_EXT_descriptor_indexing.
- Add GPU profiling.
- Add RenderDoc support.
- Add SPIRV runtime compilation (currently creating a shader involves passing in raw SPIRV, I am adding helper functions to compile from other languages into SPIRV at runtime).