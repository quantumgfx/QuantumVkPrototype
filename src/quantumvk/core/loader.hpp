#pragma once

#include "../base/vk.hpp"
#include "instance.hpp"

namespace vkq
{

    struct LoaderImpl;
    /**
	 * @brief Object explicitly representing the normally implicit Vulkan Loader object.
	*/
    class Loader
    {
    public:
        Loader() = default;
        ~Loader() = default;

        Loader(LoaderImpl* impl)
            : impl(impl)
        {
        }

    public:
        static Loader create(PFN_vkGetInstanceProcAddr getInstanceProcAddr);
        /**
         * @brief Destorys the loader and frees all associated memory.
         * 
         */
        void destroy();

        /**
         * @brief Retrieves the native, implcit, loader object (aka PFN_vkGetInstanceProcAddr).
         * 
         * @return The PFN_vkGetInstanceProcAddr used to created the loader. 
         */
        PFN_vkGetInstanceProcAddr getInstanceProcAddrLoader() const;

        /**
         * @brief Retireves a dispatcher capable of calling any global-level functions.
         * 
         * @return Gloabl Dispatcher
         */
        const vk::DispatchLoaderDynamic& getGlobalDispatch() const;

        /**
         * @brief Enumerates all avialable instance versions. If header version is less than VK_VERSION_1_1, or 
         * loader doesn't support vulkan 1.1, this returns 1.0.0;
         * 
         * @return uint32_t 
         */
        uint32_t enumerateInstanceVersion() const;

        /**
         * @brief Enumerates through all available layers
         * 
         * @return vector of layer properties.
         */
        std::vector<vk::LayerProperties> enumerateInstanceLayerProperties() const;

        /**
         * @brief Enumerates through all available extensions for a given layer.
         * 
         * @param layerName layer to check for layer instance extension
         * @return vector of extension properties
         */
        std::vector<vk::ExtensionProperties> enumerateInstanceExtensionProperties(vk::Optional<const std::string> layerName = nullptr) const;

        /**
         * @brief Create an instance handle given certain create info
         * 
         * @param createInfo info specifying creation parameters of instance
         * @return Newly created instance handle
         */
        Instance createInstance(const vk::InstanceCreateInfo& createInfo);

        LoaderImpl* getImpl() const { return impl; }

    private:
        LoaderImpl* impl = nullptr;
    };

    /**
     * @brief Global function equivelent of Loader::create();
     * Retains consistency of vk:: namespace (where instance is created by a global function as well).
     * 
     * @param getInstanceProcAddr 
     * @return Loader 
     */
    Loader createLoader(PFN_vkGetInstanceProcAddr getInstanceProcAddr);

} // namespace vkq