#include "sampler.hpp"
#include "device.hpp"

namespace Vulkan
{
	Sampler::Sampler(Device* device_, VkSampler sampler_, const SamplerCreateInfo& info)
		: Cookie(device_)
		, device(device_)
		, sampler(sampler_)
		, create_info(info)
	{
	}

	Sampler::~Sampler()
	{
		if (sampler)
		{
			if (internal_sync)
				device->DestroySamplerNolock(sampler);
			else
				device->DestroySampler(sampler);
		}
	}

	void SamplerDeleter::operator()(Sampler* sampler)
	{
		sampler->device->handle_pool.samplers.free(sampler);
	}
}