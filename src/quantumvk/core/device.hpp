#pragma once

#include "vk.hpp"
#include "instance.hpp"
#include "physicalDevice.hpp"

namespace vkq
{
    class Device
    {
        struct VkqType
        {
            vk::PhysicalDevice physicalDevice;
            vk::Device device;
            vk::DispatchLoaderDynamic dispatch;
        };


    public:

        static Device create();

    private:

        



    };
} 
