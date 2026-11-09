#pragma once

#include <vulkan/vulkan.h>

#include <string_view>
#include <vector>

namespace sidera {

class VulkanInstance {
public:
    VulkanInstance(std::string_view appName,
                   const std::vector<const char*>& requiredExtensions,
                   bool requestValidation);
    ~VulkanInstance();

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;
    VulkanInstance(VulkanInstance&&) = delete;
    VulkanInstance& operator=(VulkanInstance&&) = delete;

    [[nodiscard]] VkInstance getHandle() const noexcept { return m_instance; }
    [[nodiscard]] bool isValidationLayerActive() const noexcept { return m_validationLayerActive; }
    [[nodiscard]] bool isDebugMessengerActive() const noexcept { return m_debugMessenger != VK_NULL_HANDLE; }

private:
    [[nodiscard]] static bool checkLayerSupport(const char* layerName);
    [[nodiscard]] static bool checkExtensionSupport(const char* extensionName);
    static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) noexcept;
    void setupDebugMessenger();

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    bool m_validationLayerActive = false;
};

} // namespace sidera
