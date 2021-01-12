#pragma once

#include "../base/vk.hpp"

namespace vkq
{
    /**
	 * @brief Object explicitly representing the normally implicit Vulkan Loader object.
	*/
    class Loader
    {
    public:
        Loader() = default;
        ~Loader() = default;

    public:
        static Loader create(PFN_vkGetInstanceProcAddr getInstanceProcAddr);
        /**
         * @brief Destorys the loader and frees all associated memory.
         * 
         */
        void destroy();

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
        std::vector<vk::LayerProperties> enumerateLayerProperties() const;

        /**
         * @brief Enumerates through all available extensions for a given layer.
         * 
         * @param layerName layer to check for layer instance extension
         * @return vector of extension properties
         */
        std::vector<vk::ExtensionProperties> enumerateInstanceExtensionProperties(vk::Optional<const std::string> layerName = nullptr) const;

        /**
         * @brief Returns whether a particular layer is supported.
         * 
         * @param layerName Name of the layer.
         * @return true if the layerName is contained in the enumerateLayerProperties vector
         * @return false otherwise
         */
        bool isLayerSupported(const char* layerName) const;

        /**
         * @brief Returns whether a particular instance extension is supported.
         * 
         * @param extensionName Name of the instance extension.
         * @param layerName Optional name of the layer who provides the instance extension
         * @return true if the instance extension is supported
         * @return false otherwise
         */
        bool isInstanceExtensionSupported(const char* extensionName, vk::Optional<const std::string> layerName = nullptr) const;

        /**
         * @brief Retireves a dispatcher capable of calling any global-level functions.
         * 
         * @return Gloabl Dispatcher
         */
        const vk::DispatchLoaderDynamic& dispatch() const;

        /**
         * @brief Retrieves the native, implcit, loader object (aka PFN_vkGetInstanceProcAddr).
         * 
         * @return The PFN_vkGetInstanceProcAddr used to created the loader. 
         */
        PFN_vkGetInstanceProcAddr instanceProcAddrLoader() const;

    private:
        struct Impl
        {
            vk::DispatchLoaderDynamic dispatch;
        };

        explicit Loader(Impl* impl);

        Impl* impl_ = nullptr;
    };

} // namespace vkq