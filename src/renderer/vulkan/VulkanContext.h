#pragma once

#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanInstance.h"

#include <memory>

namespace sidera {

class Window;

class VulkanContext {
public:
    explicit VulkanContext(const Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    void waitIdle() const;

    [[nodiscard]] const VulkanInstance& getInstance() const noexcept { return m_instance; }
    [[nodiscard]] VkSurfaceKHR getSurface() const noexcept { return m_surface; }
    [[nodiscard]] const VulkanDevice& getDevice() const noexcept { return *m_device; }

private:
    VulkanInstance m_instance;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    std::unique_ptr<VulkanDevice> m_device;
};

} // namespace sidera
