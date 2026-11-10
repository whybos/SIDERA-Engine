#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sidera {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    [[nodiscard]] bool isComplete() const noexcept {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

class VulkanDevice {
public:
    VulkanDevice(VkInstance instance, VkSurfaceKHR surface);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) = delete;
    VulkanDevice& operator=(VulkanDevice&&) = delete;

    void waitIdle() const;

    [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const noexcept { return m_physicalDevice; }
    [[nodiscard]] VkDevice getLogicalDevice() const noexcept { return m_device; }
    [[nodiscard]] VkQueue getGraphicsQueue() const noexcept { return m_graphicsQueue; }
    [[nodiscard]] VkQueue getPresentQueue() const noexcept { return m_presentQueue; }
    [[nodiscard]] const QueueFamilyIndices& getQueueFamilies() const noexcept { return m_queueFamilies; }
    [[nodiscard]] const VkPhysicalDeviceProperties& getProperties() const noexcept { return m_properties; }
    [[nodiscard]] bool isDynamicRenderingEnabled() const noexcept { return m_dynamicRenderingEnabled; }
    [[nodiscard]] bool isSamplerAnisotropyEnabled() const noexcept { return m_samplerAnisotropyEnabled; }
    [[nodiscard]] bool isMultiDrawIndirectEnabled() const noexcept { return m_multiDrawIndirectEnabled; }
    [[nodiscard]] bool isDrawIndirectFirstInstanceEnabled() const noexcept { return m_drawIndirectFirstInstanceEnabled; }
    [[nodiscard]] bool isDrawIndirectCountSupported() const noexcept { return m_drawIndirectCountSupported; }
    [[nodiscard]] bool isFillModeNonSolidSupported() const noexcept { return m_fillModeNonSolidEnabled; }
    [[nodiscard]] bool isTextureCompressionBCSupported() const noexcept { return m_textureCompressionBCEnabled; }
    [[nodiscard]] bool isDescriptorIndexingSupported() const noexcept { return m_descriptorIndexingSupported; }

    [[nodiscard]] VkFormatProperties getFormatProperties(VkFormat format) const noexcept;
    [[nodiscard]] bool isFormatSupported(VkFormat format, VkFormatFeatureFlags flags) const noexcept;
    [[nodiscard]] bool isLinearBlitSupported(VkFormat format) const noexcept;

private:
    void pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
    void createLogicalDevice();

    [[nodiscard]] int scorePhysicalDevice(VkPhysicalDevice device, VkSurfaceKHR surface) const;
    [[nodiscard]] static QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
    [[nodiscard]] static bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    [[nodiscard]] static bool checkVulkan13FeatureSupport(VkPhysicalDevice device);

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    QueueFamilyIndices m_queueFamilies{};
    VkPhysicalDeviceProperties m_properties{};
    bool m_dynamicRenderingEnabled = false;
    bool m_samplerAnisotropyEnabled = false;
    bool m_multiDrawIndirectEnabled = false;
    bool m_drawIndirectFirstInstanceEnabled = false;
    bool m_drawIndirectCountSupported = false;
    bool m_fillModeNonSolidEnabled = false;
    bool m_textureCompressionBCEnabled = false;
    bool m_descriptorIndexingSupported = false;

    static const std::vector<const char*> s_requiredDeviceExtensions;
};

} // namespace sidera
