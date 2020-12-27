# QuantumVk

QuantumVk is a lightweight library meant to speed up Vulkan development, by removing boilerplate code and providing for common use cases.
All native handles are completely exposed, so the library imposes no restrictions on how you can use Vulkan. QuantumVk uses C++17, 
and the Vulkan C++ API.

# Dependencies

All of these file paths can be overriden in the root CMakeLists.txt

- [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers): Vulkan API Headers from the Khronos Group's github repository
- [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator): AMD's Vulkan Memory Allocator is used to ease construction of resources bound to memory

#### So what about the Vulkan SDK?

QuantumVk doesn't depend on the [Vulkan SDK](https://vulkan.lunarg.com/). It retireves the most up-to-date headers from the offical github repository.
However, a compatible Vulkan Loader is still required, as well as appropriate ICDs. The SDK is still needed
for any validation layers to be enabled.

# Features

- Wrappers for all dispatchable vk handles, and automated Vulkan function pointer loading.
- Compatible with any version of vulkan, or any set of extensions 
    - When a wrapper is part of a specific extension or version, it is marked as such
- Custom memory allocation manager, built off of the [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
    - Vma structures and handles are still exposed and can be used natively
    - All vk memory functions are still available

# Project Structure

## Core

The `/core` directory contains the core functionality for QuantumVk, and the basic wrappers around standard types:
Core contains the core functionality and wrappers around Vulkan types. There are several types of wrappers. Extension wrappers
are only compiled if the available extensions are defined in the given `Vulkan-Headers`

##### Opaque Handles

Handles around objects that must hold data in addition to the `vk::` handle (examples of this data includes function pointers for method calls).
These wrappers have a static create() function, as well as a destroy() method. The create function takes in a `vk::` createInfo, as well as any 
parent handles that the wrapper needs. There are options for passing in both native `vk::` handles, as well as `vkq::` wrapper handles as parents. 
Opaque handles are small and easy to pass around (they are essentially just pointers) but have a bit of additional overhead (as a heap allocation 
is needed), and must be created and destroyed manually. Examples include:

- `vkq::Loader`
- `vkq::Instance`
- `vkq::Device`

##### Dispatchable Wrappers

Handles around dispatchable objects that aren't created, nor destroyed (aka `vk::PhysicalDevice` and `vk::Queue`) They merely exist as long as 
their parent exists (`vk::Instance` and `vk::Device` respectively). These wrappers contain a reference to a parent, so that function pointers
can still be accessed.

##### Simple Wrappers

These are just simple wrappers around basic, non-dispatchable vulkan types. They contain no additional data, and are in all respects equivelent to 
the standard `vk::` handles types. They are just to keep the vkq namespace consistent. The are created simply by constructor or assignment operator
Examples include:

- `vkq::DebugUtilsMessengerEXT`
- `vkq::SurfaceKHR`