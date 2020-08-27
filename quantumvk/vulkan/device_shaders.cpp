#include "device.hpp"

#include "quantumvk/utils/bitops.hpp"
#include "quantumvk/utils/hash.hpp"

namespace Vulkan
{
    ShaderHandle Device::CreateShader(const uint32_t* code, size_t size)
    {
        return ShaderHandle(handle_pool.shaders.allocate(this, code, size));
    }

	ProgramHandle Device::CreateGraphicsProgram(const GraphicsProgramShaders& shaders)
	{
		return ProgramHandle(handle_pool.programs.allocate(this, shaders));
	}

	ProgramHandle Device::CreateComputeProgram(const ComputeProgramShaders& shaders)
	{
		return ProgramHandle(handle_pool.programs.allocate(this, shaders));
	}

}