#include <quantumvk.hpp>

#include <iostream>
#include <thread>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

static void fb_size_cb(GLFWwindow* window, int width, int height);

struct GLFWPlatform : public Vulkan::WSIPlatform
{

	GLFWPlatform()
	{
		width = 1280;
		height = 720;
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		window = glfwCreateWindow(width, height, "GLFW Window", nullptr, nullptr);

		glfwSetWindowUserPointer(window, this);
		glfwSetFramebufferSizeCallback(window, fb_size_cb);
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
		return !glfwWindowShouldClose(window);
	}

	virtual void PollInput()
	{
		glfwPollEvents();
	}

	void NotifyResize(int width_, int height_)
	{
		resize = true;
		width = static_cast<uint32_t>(width_);
		height = static_cast<uint32_t>(height_);
	}

private:

	GLFWwindow* window = nullptr;
	unsigned width = 0;
	unsigned height = 0;

};

static void fb_size_cb(GLFWwindow* window, int width, int height)
{
	auto* glfw = static_cast<GLFWPlatform*>(glfwGetWindowUserPointer(window));
	glfw->NotifyResize(width, height);
}

int main() 
{
	glfwInit();

	if (!Vulkan::Context::InitLoader(nullptr))
		QM_LOG_ERROR("Failed to load vulkan dynamic library");

	{
		GLFWPlatform platform;

		Vulkan::WSI wsi;
		wsi.SetPlatform(&platform);
		wsi.SetBackbufferSrgb(true);
		wsi.Init(1, nullptr, 0);

		{
			Vulkan::Device& device = wsi.GetDevice();


			const char* vertex_code = R"(
#version 450

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec3 in_col;

layout(location = 0) out vec3 frag_col;

layout(set = 0, binding = 0) uniform UBO {
	
	vec2 offset;

} off;


void main()
{

	gl_Position = vec4(in_pos.x + off.offset.x, in_pos.y + off.offset.y, 0.0, 1.0);
	frag_col = in_col;

}
)";
			Vulkan::ShaderHandle vert_shader = device.CreateShaderGLSL(vertex_code, Vulkan::ShaderStage::Vertex);


			const char* frag_code = R"(
#version 450

layout(location = 0) in vec3 frag_col;

layout(location = 0) out vec4 out_color;

void main()
{
	out_color = vec4(frag_col, 1.0);
}
)";
			Vulkan::ShaderHandle frag_shader = device.CreateShaderGLSL(frag_code, Vulkan::ShaderStage::Fragment);

			Vulkan::GraphicsProgramShaders p_shaders;
			p_shaders.vertex = vert_shader;
			p_shaders.fragment = frag_shader;

			Vulkan::ProgramHandle program = device.CreateGraphicsProgram(p_shaders);


			while (platform.Alive(wsi))
			{

				Util::Timer timer;
				timer.start();

				wsi.BeginFrame();
				{
					auto cmd = device.RequestCommandBuffer();

					// Just render a clear color to screen.
					// There is a lot of stuff going on in these few calls which will need its own sample to explore w.r.t. synchronization.
					// For now, you'll just get a blue-ish color on screen.
					Vulkan::RenderPassInfo rp = device.GetSwapchainRenderPass(Vulkan::SwapchainRenderPass::ColorOnly);
					rp.clear_color[0].float32[0] = 0.1f;
					rp.clear_color[0].float32[1] = 0.2f;
					rp.clear_color[0].float32[2] = 0.3f;
					cmd->BeginRenderPass(rp);

					cmd->SetOpaqueState();

					cmd->SetProgram(program);
					cmd->SetVertexAttrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
					cmd->SetVertexAttrib(1, 0, VK_FORMAT_R32G32B32_SFLOAT, 8);
					cmd->SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
					cmd->SetCullMode(VK_CULL_MODE_NONE);

					float vert_data[30] = { -0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
											0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
											0.0f, 0.5f, 0.0f, 0.0f, 1.0f,
											-1.0f, -1.0f, 1.0f, 1.0f, 1.0f,
											1.0f, -1.0f, 0.0f, 1.0f, 1.0f,
											0.0f, 0.0f, 0.0f, 0.0f, 1.0f };


					void* vertex_data = cmd->AllocateVertexData(0, sizeof(float) * 10 * 6, sizeof(float) * 5);

					memcpy(vertex_data, vert_data, sizeof(float) * 5 * 6);

					void* uniform_data = cmd->AllocateConstantData(0, 0, 8);

					float offsets[2] = { 0.0, 0.0 };
					memcpy(uniform_data, offsets, 8);

					cmd->Draw(6);

					cmd->EndRenderPass();
					device.Submit(cmd);
				}

				wsi.EndFrame();

				float time_milli = timer.end() * 1000;

				//QM_LOG_INFO("Frame time (ms): %f\n", time_milli);
			}
		}

	}

	glfwTerminate();
	
}