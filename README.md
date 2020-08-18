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

# Design

## Lazy On-Demand Creation
Something QuantumVk does quite often is lazily create objects and resources. Particularly with the render state. It is just very hard and annoying to be
completely explicit 100% of the time. Instead of having you (the user) fill in the entire VkGraphicsPipelineCreateInfo structure, fill out the VkPipelineLayout,
explicity call vkCreateGraphicsPipelines, manage some pipeline object, and make sure it isn't in use when you delete it, QuantumVk abstracts this all away via 
lazy creation and hashmaps and caches. QuantumVk instead has you set some basic state info in the command buffer, and either generates a
new renderpass/pipeline or retrieves a previously used resource via hashmaps.

# Current Work
The basic library is now functional. I am currently working on removing previous limitations and adding more features.
My current TODO list includes...

- Multiplatform support (probably Mac OSX via MoltenVk first)
- Adding GPU profiling
- Adding complete QuantumVk API support for the following extensions:
	- VK_EXT_descriptor_indexing (so that bindless is supported)
	- VK_KHR_ray_tracing (raytracing is still in beta, so maybe in some kind of optional implementation?)
	- VK_NV_mesh_shader