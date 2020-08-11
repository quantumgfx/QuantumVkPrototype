#include <quantumvk.hpp>

#include <iostream>

int main() 
{
	if (Vulkan::Context::InitLoader(nullptr))
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
	delete context;
}