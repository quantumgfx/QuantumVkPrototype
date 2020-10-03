# QuantumVk
QuantumVk is a fast and flexible middleware library for Vulkan, written in C++17. It seeks to improve
development, remove boiler-plate and provide a higher-level API for Vulkan, without sacrificing Vulkan's unique advantage: its speed.
QuantumVk takes inspiration from [Granite Engine](https://github.com/Themaister/Granite), created by the Themaister
(specifically Granite's Vulkan backend).

For examples of QuantumVk in use, check out the [QuantumVkExamples](https://github.com/quantumgfx/QuantumVkExamples) repository.

# Building
In order to build QuantumVk, you must have downloaded the following, regardless of platform:
- [Git](https://git-scm.com/downloads): To retrieve the repository.
- [Cmake](https://cmake.org/): QuantumVk uses Cmake as a build system.
- [Vulkan SDK](https://vulkan.lunarg.com/): The vulkan sdk is used to access vulkan functionality. QuantumVk doesn't link directly to the vulkan library. Instead [volk](https://github.com/zeux/volk) is used to link dynamically to the vulkan .dll/.so/.dylib depending on the platform.

#### Windows
Windows is currently the only tested platform, though Linux, Andriod, Mac OSX and ios support is currently being worked on (Mac and ios via MoltenVk).
- [Visual Studio 2019](https://visualstudio.microsoft.com/vs/): The suggested developement IDE on Windows for QuantumVk.

# Current Work
The basic library is now functional. I am currently working on removing previous limitations and adding more features.
My current TODO list includes...

- Multiplatform support (probably Mac OSX via MoltenVk first)
- Adding GPU profiling
- Support for multiple WSIs simultaneuosly
- Adding complete QuantumVk API support for the following extensions:
	- VK_EXT_descriptor_indexing (so that bindless is supported)
	- VK_KHR_ray_tracing (raytracing is still in beta, so maybe in some kind of optional implementation?)
	- VK_NV_mesh_shader