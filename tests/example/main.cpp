#include <quantumvk.hpp>

#include <iostream>

int main() 
{
	if (Vulkan::Context::InitLoader(nullptr))
		std::cout << "Initing Vulkan loader successful\n";
	else
		std::cout << "Initing Vulkan loader usuccessful\n";

	{
		Vulkan::Context context;

		context.InitInstanceAndDevice(nullptr, 0, nullptr, 0);
	}
}