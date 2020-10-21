#pragma once

#include "quantumvk/utils/hash.hpp"
#include "quantumvk/utils/object_pool.hpp"
#include "quantumvk/utils/intrusive.hpp"
#include "quantumvk/utils/intrusive_object_pool.hpp"
#include "quantumvk/utils/temporary_hashmap.hpp"

#include "quantumvk/vulkan/images/sampler.hpp"

#include "quantumvk/vulkan/misc/limits.hpp"
#include "quantumvk/vulkan/misc/cookie.hpp"

#include "quantumvk/vulkan/vulkan_headers.hpp"

#include <utility>
#include <vector>

namespace Vulkan
{
	static const unsigned VULKAN_NUM_SETS_PER_POOL = 16;
	static const unsigned VULKAN_DESCRIPTOR_RING_SIZE = 8;

	//Forward declare Device
	class Device;
	class Program;
	//Descriptor set layout
	struct DescriptorSetLayout
	{
		//Size of array at each binding
		uint32_t array_size[VULKAN_NUM_BINDINGS] = {};

		//Location of all sampled images
		uint32_t sampled_image_mask = 0;
		//Location of all storage images
		uint32_t storage_image_mask = 0;
		//Location of all uniform buffers
		uint32_t uniform_buffer_mask = 0;
		//Location of all storage buffers
		uint32_t storage_buffer_mask = 0;
		//Location of all texel buffer views
		uint32_t sampled_buffer_mask = 0;
		//Location of input attachments
		uint32_t input_attachment_mask = 0;
		//Location of all samplers
		uint32_t sampler_mask = 0;
		//Location of non combined images
		uint32_t separate_image_mask = 0;
		//Which images are floating point and which are integer formats
		uint32_t fp_mask = 0;
		//Location of immutable samplers
		uint32_t immutable_sampler_mask = 0;
		//Type of each immutable sampler
		uint64_t immutable_samplers = 0;
	};

	//Returns whether the set layout has an immutable sampler at binding
	static inline bool HasImmutableSampler(const DescriptorSetLayout& layout, unsigned binding)
	{
		return (layout.immutable_sampler_mask & (1u << binding)) != 0;
	}

	//Returns immutable sampler type at binding
	static inline StockSampler GetImmutableSampler(const DescriptorSetLayout& layout, unsigned binding)
	{
		VK_ASSERT(HasImmutableSampler(layout, binding));
		return static_cast<StockSampler>((layout.immutable_samplers >> (4 * binding)) & 0xf);
	}

	//Sets immutable sampler type at binding
	static inline void SetImmutableSampler(DescriptorSetLayout& layout, unsigned binding, StockSampler sampler)
	{
		layout.immutable_samplers |= uint64_t(sampler) << (4 * binding);
		layout.immutable_sampler_mask |= 1u << binding;
	}

	class ImageView;

	// Represents a single descriptor in a descriptor set. 
	struct ResourceBinding
	{
		union
		{
			VkDescriptorBufferInfo buffer;
			VkDescriptorImageInfo image;
			VkBufferView buffer_view;
		};
		VkDeviceSize dynamic_offset;

		// Primary object cookie
		uint64_t cookie = 0;
		// Secondary object cookie (for example: sampler)
		uint64_t secondary_cookie = 0;
	};

	class ResourceManager
	{
	public:

		ResourceManager()
			: resource_array(nullptr)
		{
		}
		~ResourceManager()
		{
			if (resource_array)
				delete[] resource_array;
		}

		void CreateResourceArray(uint32_t resource_count)
		{
			if (resource_array)
				delete[] resource_array;
			resource_array = new ResourceBinding[resource_count];
		}

		ResourceBinding* GetResourceArray() { return resource_array; }

	private:

		ResourceBinding* resource_array;
	};


	class UniformManager
	{
	public:

		QM_NO_MOVE_NO_COPY(UniformManager)
		
		UniformManager();
		~UniformManager();

		void InitUniforms(Device* device, Program& program);

		ResourceBinding& GetUniformResource(uint32_t thread_index, uint32_t set, uint32_t binding, uint32_t array_index);
		void SetUniformResource(uint32_t thread_index, uint32_t set, uint32_t binding, uint32_t array_index, const ResourceBinding& resource);
		VkDescriptorSet FlushDescriptorSet(uint32_t thread_index, uint32_t set);

		inline const VkPushConstantRange& GetPushConstantRange() const { return push_constant_range; }
		inline VkPipelineLayout           GetUniformLayout() const { return uniform_layout; }
		inline const DescriptorSetLayout& GetSetLayout(uint32_t set) const { return sets[set].layout; }
		inline uint32_t                   GetDescriptorSetMask() const { return descriptor_set_mask; }

		inline bool HasDescriptorSet(uint32_t set) const { return descriptor_set_mask & (1u << set); }
		inline bool HasDescriptorBinding(uint32_t set, uint32_t binding) const { return sets[set].binding_stages != 0; }
		inline uint32_t GetDescriptorBindingArraySize(uint32_t set, uint32_t binding) const { return sets[set].layout.array_size[binding]; }
		inline bool IsFloatDescriptor(uint32_t set, uint32_t binding) const { return sets[set].layout.fp_mask & (1u << binding); }


		void BeginFrame();
		void Clear();

	private:

		void CheckForNewThread(uint32_t thread_index);

		struct HashedDescriptorSet
		{
			VkDescriptorSet vk_set = VK_NULL_HANDLE;
			bool needs_update = false;
		};

		HashedDescriptorSet FindDescriptorSet(uint32_t thread_index, uint32_t set);
		void UpdateDescriptorSetLegacy(uint32_t thread_index, uint32_t set, VkDescriptorSet desc_set);

	public:

		struct DescriptorSetNode : Util::TemporaryHashmapEnabled<DescriptorSetNode>, Util::IntrusiveListEnabled<DescriptorSetNode>
		{
			explicit DescriptorSetNode(VkDescriptorSet set_)
				: set(set_)
			{
			}

			VkDescriptorSet set;
		};

		struct PerThreadPerSet
		{
			Util::TemporaryHashmap<DescriptorSetNode, VULKAN_DESCRIPTOR_RING_SIZE, true> set_nodes;
			std::vector<VkDescriptorPool> pools;
			bool should_begin = true;
		};

		struct PerThread
		{
			ResourceManager manager;
			bool active = false;
		};

		struct PerSet
		{
			VkShaderStageFlags stages = 0;
			VkShaderStageFlags binding_stages[VULKAN_NUM_BINDINGS] = {};
			DescriptorSetLayout layout;

			VkDescriptorUpdateTemplateKHR update_template;

			VkDescriptorSetLayout vk_set_layout = VK_NULL_HANDLE;
			std::vector<VkDescriptorPoolSize> pool_size;

			std::vector<std::unique_ptr<PerThreadPerSet>> threads;

		};

	private:

		Device* device = nullptr;

		uint32_t descriptor_set_count = 0;
		uint32_t descriptor_set_mask = 0;

		std::vector<PerSet> sets;

		uint32_t resource_count = 0;
		uint32_t resource_offsets[VULKAN_NUM_DESCRIPTOR_SETS][VULKAN_NUM_BINDINGS] = {};

		std::vector<PerThread> threads;

		VkPipelineLayout uniform_layout = VK_NULL_HANDLE;
		VkPushConstantRange push_constant_range = {};

	};
}