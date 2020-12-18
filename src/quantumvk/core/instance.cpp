#include "instance.hpp"

namespace vkq
{
    ////////////////////////////////
    // Instance ////////////////////
    ////////////////////////////////

    Instance Instance::create(Loader loader, const vk::InstanceCreateInfo& createInfo)
    {
        Instance instance{ new Instance::VkqType() };

        instance.type->dispatch = loader.getGlobalDispatch();
        instance.type->instance = vk::createInstance(createInfo, nullptr, instance.type->dispatch);

        return instance;
    }

    void Instance::destroy()
    {
        type->instance.destroy(nullptr, type->dispatch);
        delete type;
        type = nullptr;

    }

    /////////////////////////////////
    // Instance Factory /////////////
    /////////////////////////////////

    InstanceFactory::InstanceFactory(Loader loader)
    {
    }

    InstanceFactory& InstanceFactory::requireApiVersion(uint32_t version)
    {
        // TODO: insert return statement here
    }
    InstanceFactory& InstanceFactory::requestApiVersion(uint32_t version)
    {
        // TODO: insert return statement here
    }
    InstanceFactory& InstanceFactory::enableLayer(const char* layerName)
    {
        // TODO: insert return statement here
    }

    InstanceFactory& InstanceFactory::enableExtension(const char* extensionName)
    {
        // TODO: insert return statement here
    }
}