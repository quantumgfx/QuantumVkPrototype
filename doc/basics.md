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
Note: Context::InitLoader returns false if it fails and true otherwise.