#include <quantumvk.hpp>

#include <iostream>
#include <thread>

#include <GLFW/glfw3.h>

struct GLFWPlatform : public Vulkan::WSIPlatform
{

	GLFWPlatform()
	{
		width = 1280;
		height = 720;
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
		window = glfwCreateWindow(width, height, "GLFW Window", nullptr, nullptr);
	}

	virtual ~GLFWPlatform() 
	{
		if (window)
			glfwDestroyWindow(window);
	}

	virtual VkSurfaceKHR CreateSurface(VkInstance instance, VkPhysicalDevice gpu)
	{
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
			return VK_NULL_HANDLE;

		int actual_width, actual_height;
		glfwGetFramebufferSize(window, &actual_width, &actual_height);
		width = unsigned(actual_width);
		height = unsigned(actual_height);
		return surface;
	}

	virtual std::vector<const char*> GetInstanceExtensions()
	{
		uint32_t count;
		const char** ext = glfwGetRequiredInstanceExtensions(&count);
		return { ext, ext + count };
	}

	virtual uint32_t GetSurfaceWidth()
	{
		return width;
	}

	virtual uint32_t GetSurfaceHeight()
	{
		return height;
	}

	virtual bool Alive(Vulkan::WSI& wsi)
	{
		glfwPollEvents();
		return !glfwWindowShouldClose(window);
	}

	virtual void PollInput()
	{
		glfwPollEvents();
	}

	GLFWwindow* window = nullptr;
	unsigned width = 0;
	unsigned height = 0;

};

int main() 
{
	glfwInit();

	{
		GLFWPlatform platform;


		while (!glfwWindowShouldClose(platform.window)) 
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
			platform.PollInput();
		}
	}
	


	glfwTerminate();



	/*if (Vulkan::Context::InitLoader(nullptr))
		std::cout << "Initing Vulkan loader successful\n";
	else
		std::cout << "Initing Vulkan loader usuccessful\n";

	Vulkan::Context* context = new Vulkan::Context();
	context->InitInstanceAndDevice(nullptr, 0, nullptr, 0);
	Vulkan::Device* device = new Vulkan::Device();
	device->SetContext(context, nullptr, 0);

	{

		Vulkan::ShaderHandle vert_shader = device->CreateShader(nullptr, 0);
		Vulkan::ShaderHandle frag_shader = device->CreateShader(nullptr, 0);

		Vulkan::GraphicsProgramShaders program_shaders;
		program_shaders.vertex = vert_shader;
		program_shaders.fragment = frag_shader;

		Vulkan::ProgramHandle program = device->CreateGraphicsProgram(program_shaders);

	}

	delete device;
	delete context;*/
}