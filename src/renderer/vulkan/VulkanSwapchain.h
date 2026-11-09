#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace sidera {

class VulkanDevice;

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanSwapchain {
public:
    VulkanSwapchain(const VulkanDevice& device,
                    VkSurfaceKHR surface,
                    uint32_t width,
                    uint32_t height,
                    bool vsync = true);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    VulkanSwapchain(VulkanSwapchain&&) = delete;
    VulkanSwapchain& operator=(VulkanSwapchain&&) = delete;

    void recreate(uint32_t width, uint32_t height);

    [[nodiscard]] VkResult acquireNextImage(VkSemaphore imageAvailableSemaphore,
                                            uint32_t* outImageIndex) const;
    [[nodiscard]] VkResult present(uint32_t imageIndex,
                                   VkSemaphore renderFinishedSemaphore) const;

    [[nodiscard]] VkSwapchainKHR getHandle() const noexcept { return m_swapchain; }
    [[nodiscard]] VkFormat getImageFormat() const noexcept { return m_imageFormat; }
    [[nodiscard]] VkColorSpaceKHR getColorSpace() const noexcept { return m_colorSpace; }
    [[nodiscard]] VkPresentModeKHR getPresentMode() const noexcept { return m_presentMode; }
    [[nodiscard]] VkExtent2D getExtent() const noexcept { return m_extent; }
    [[nodiscard]] uint32_t getImageCount() const noexcept { return static_cast<uint32_t>(m_images.size()); }
    [[nodiscard]] VkImage getImage(uint32_t index) const noexcept { return m_images[index]; }
    [[nodiscard]] VkImageView getImageView(uint32_t index) const noexcept { return m_imageViews[index]; }

private:
    void createSwapchain(uint32_t width, uint32_t height, VkSwapchainKHR oldSwapchain);
    void createImageViews();
    void destroyImageViews() noexcept;

    [[nodiscard]] SwapchainSupportDetails querySwapchainSupport() const;
    [[nodiscard]] static VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    [[nodiscard]] VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
    [[nodiscard]] static VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                                 uint32_t width,
                                                 uint32_t height) noexcept;

    const VulkanDevice& m_device;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    bool m_vsync = true;
    bool m_loggedInitialCapabilities = false;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR m_colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D m_extent{0, 0};

    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
};

} // namespace sidera
