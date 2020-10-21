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
		ProgramHandle program = ProgramHandle(handle_pool.programs.allocate(this, shaders));

#ifdef QM_VULKAN_MT
		std::lock_guard holder_{ lock.program_lock };
#endif
		if (!invalid_programs.empty())
		{
			active_programs[invalid_programs.back()] = program;
			invalid_programs.pop_back();
		}
		else
		{
			active_programs.push_back(program);
		}
			
		return program;
	}

	ProgramHandle Device::CreateComputeProgram(const ComputeProgramShaders& shaders)
	{
		ProgramHandle program = ProgramHandle(handle_pool.programs.allocate(this, shaders));

#ifdef QM_VULKAN_MT
		std::lock_guard holder_{ lock.program_lock };
#endif
		if (!invalid_programs.empty())
		{
			active_programs[invalid_programs.back()] = program;
			invalid_programs.pop_back();
		}
		else
		{
			active_programs.push_back(program);
		}

		return program;
	}

	void Device::UpdateInvalidProgramsNoLock()
	{
#ifdef QM_VULKAN_MT
		std::lock_guard holder_{ lock.program_lock };
#endif
		// Always called inside device

		for (uint32_t i = 0; i < active_programs.size(); i++)
		{
			ProgramHandle& program = active_programs[i];
			if(program)
				// If this is the only reference left
				if (program->GetRefCount() == 1)
				{
					program.Reset();
					invalid_programs.push_back(i);
				}
		}
	}

}