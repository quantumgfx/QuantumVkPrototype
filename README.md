# QuantumVk
QuantumVk is a fast and flexible middle-ware library for Vulkan, written in C++17. It seeks to improve
development, remove boiler-plate and provide a higher-level API for Vulkan, without sacrificing Vulkan's unique advantage: its speed.
QuantumVk takes inspiration from (and sometimes straight up copies :)) [Granite Engine](https://github.com/Themaister/Granite), created by the Themaister
(specifically Granite's Vulkan backend).

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

- Add SPIRV runtime compilation (currently creating a shader involves passing in raw SPIRV, I am adding helper functions to compile from other languages into SPIRV at runtime via SPIRV-Cross).
- Add descriptor set arrays. Currently arrays are only supported for bindless descriptor sets (which requires an extension).
- Fix persistent VulkanCaches (Many resources like programs and shaders are cached for the entire lifetime of the Device. Whilst pipelines need to be cached with their respective programs, I feel like programs should be a handle not a cache.)