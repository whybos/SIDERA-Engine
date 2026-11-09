#include "renderer/vulkan/VulkanSwapchain.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace sidera {

namespace {
constexpr std::string_view formatToString(VkFormat format) noexcept {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_SRGB:           return "VK_FORMAT_B8G8R8A8_SRGB";
        case VK_FORMAT_R8G8B8A8_SRGB:           return "VK_FORMAT_R8G8B8A8_SRGB";
        case VK_FORMAT_B8G8R8A8_UNORM:          return "VK_FORMAT_B8G8R8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_UNORM:          return "VK_FORMAT_R8G8B8A8_UNORM";
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
        case VK_FORMAT_R16G16B16A16_SFLOAT:     return "VK_FORMAT_R16G16B16A16_SFLOAT";
        default:                                return "VK_FORMAT_OTHER";
    }
}

constexpr std::string_view colorSpaceToString(VkColorSpaceKHR colorSpace) noexcept {
    switch (colorSpace) {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
        default:                                return "VK_COLOR_SPACE_OTHER";
    }
}

constexpr std::string_view presentModeToString(VkPresentModeKHR mode) noexcept {
    switch (mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:                 return "VK_PRESENT_MODE_IMMEDIATE_KHR";
        case VK_PRESENT_MODE_MAILBOX_KHR:                   return "VK_PRESENT_MODE_MAILBOX_KHR";
        case VK_PRESENT_MODE_FIFO_KHR:                      return "VK_PRESENT_MODE_FIFO_KHR";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:              return "VK_PRESENT_MODE_FIFO_RELAXED_KHR";
        default:                                            return "VK_PRESENT_MODE_OTHER";
    }
}
} // namespace

VulkanSwapchain::VulkanSwapchain(const VulkanDevice& device,
                                 VkSurfaceKHR surface,
                                 uint32_t width,
                                 uint32_t height,
                                 bool vsync)
    : m_device(device)
    , m_surface(surface)
    , m_vsync(vsync) {
    createSwapchain(width, height, VK_NULL_HANDLE);
    createImageViews();
}

VulkanSwapchain::~VulkanSwapchain() {
    SIDERA_LOG_INFO("Swapchain", "Destroying swapchain image views ({})...", m_imageViews.size());
    destroyImageViews();

    if (m_swapchain != VK_NULL_HANDLE) {
        SIDERA_LOG_INFO("Swapchain", "Destroying Vulkan swapchain...");
        vkDestroySwapchainKHR(m_device.getLogicalDevice(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void VulkanSwapchain::recreate(uint32_t width, uint32_t height) {
    m_device.waitIdle();

    VkSwapchainKHR oldSwapchain = m_swapchain;
    destroyImageViews();

    createSwapchain(width, height, oldSwapchain);

    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device.getLogicalDevice(), oldSwapchain, nullptr);
    }

    createImageViews();

    SIDERA_LOG_INFO(
        "Swapchain",
        "Swapchain recreated ({}x{} px, {} images).",
        m_extent.width,
        m_extent.height,
        m_images.size()
    );
}

VkResult VulkanSwapchain::acquireNextImage(VkSemaphore imageAvailableSemaphore,
                                           uint32_t* outImageIndex) const {
    const VkResult result = vkAcquireNextImageKHR(
        m_device.getLogicalDevice(),
        m_swapchain,
        std::numeric_limits<uint64_t>::max(),
        imageAvailableSemaphore,
        VK_NULL_HANDLE,
        outImageIndex
    );

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR) {
        VK_CHECK(result, "Failed to acquire next swapchain image");
    }

    return result;
}

VkResult VulkanSwapchain::present(uint32_t imageIndex,
                                  VkSemaphore renderFinishedSemaphore) const {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr;

    const VkResult result = vkQueuePresentKHR(m_device.getPresentQueue(), &presentInfo);

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR) {
        VK_CHECK(result, "Failed to present swapchain image");
    }

    return result;
}

void VulkanSwapchain::createSwapchain(uint32_t width, uint32_t height, VkSwapchainKHR oldSwapchain) {
    const SwapchainSupportDetails support = querySwapchainSupport();

    if (support.formats.empty() || support.presentModes.empty()) {
        throw std::runtime_error("Swapchain surface support is inadequate (no formats or present modes).");
    }

    if (!m_loggedInitialCapabilities) {
        SIDERA_LOG_INFO(
            "Swapchain",
            "Surface capabilities: minImageCount={}, maxImageCount={}, currentExtent={}x{}, "
            "supportedFormats={}, supportedPresentModes={}",
            support.capabilities.minImageCount,
            support.capabilities.maxImageCount,
            support.capabilities.currentExtent.width,
            support.capabilities.currentExtent.height,
            support.formats.size(),
            support.presentModes.size()
        );
        m_loggedInitialCapabilities = true;
    }

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    const VkExtent2D extent = chooseExtent(support.capabilities, width, height);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const QueueFamilyIndices& indices = m_device.getQueueFamilies();
    const uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;

    VK_CHECK(
        vkCreateSwapchainKHR(m_device.getLogicalDevice(), &createInfo, nullptr, &m_swapchain),
        "Failed to create Vulkan swapchain"
    );

    uint32_t actualImageCount = 0;
    VK_CHECK(
        vkGetSwapchainImagesKHR(m_device.getLogicalDevice(), m_swapchain, &actualImageCount, nullptr),
        "Failed to query swapchain image count"
    );

    m_images.resize(actualImageCount);
    VK_CHECK(
        vkGetSwapchainImagesKHR(m_device.getLogicalDevice(), m_swapchain, &actualImageCount, m_images.data()),
        "Failed to retrieve swapchain images"
    );

    m_imageFormat = surfaceFormat.format;
    m_colorSpace = surfaceFormat.colorSpace;
    m_presentMode = presentMode;
    m_extent = extent;

    if (oldSwapchain == VK_NULL_HANDLE) {
        SIDERA_LOG_INFO(
            "Swapchain",
            "Created swapchain: {}x{} px | Images: {} | Format: {} | ColorSpace: {} | PresentMode: {}",
            m_extent.width,
            m_extent.height,
            m_images.size(),
            formatToString(m_imageFormat),
            colorSpaceToString(m_colorSpace),
            presentModeToString(m_presentMode)
        );
    }
}

void VulkanSwapchain::createImageViews() {
    m_imageViews.resize(m_images.size(), VK_NULL_HANDLE);

    for (size_t i = 0; i < m_images.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_images[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_imageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VK_CHECK(
            vkCreateImageView(m_device.getLogicalDevice(), &createInfo, nullptr, &m_imageViews[i]),
            "Failed to create swapchain image view"
        );
    }
}

void VulkanSwapchain::destroyImageViews() noexcept {
    for (VkImageView view : m_imageViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device.getLogicalDevice(), view, nullptr);
        }
    }
    m_imageViews.clear();
}

SwapchainSupportDetails VulkanSwapchain::querySwapchainSupport() const {
    SwapchainSupportDetails details;
    VkPhysicalDevice physicalDevice = m_device.getPhysicalDevice();

    VK_CHECK(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_surface, &details.capabilities),
        "Failed to query physical device surface capabilities"
    );

    uint32_t formatCount = 0;
    VK_CHECK(
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount, nullptr),
        "Failed to query surface format count"
    );
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        VK_CHECK(
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_surface, &formatCount, details.formats.data()),
            "Failed to query surface formats"
        );
    }

    uint32_t presentModeCount = 0;
    VK_CHECK(
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_surface, &presentModeCount, nullptr),
        "Failed to query surface present mode count"
    );
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        VK_CHECK(
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice, m_surface, &presentModeCount, details.presentModes.data()
            ),
            "Failed to query surface present modes"
        );
    }

    return details;
}

VkSurfaceFormatKHR VulkanSwapchain::chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_R8G8B8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return availableFormats.front();
}

VkPresentModeKHR VulkanSwapchain::choosePresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes) const {
    if (!m_vsync) {
        for (VkPresentModeKHR mode : availablePresentModes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
    }

    // VK_PRESENT_MODE_FIFO_KHR is guaranteed by the Vulkan specification to be supported
    // and paces frame presentation to display refresh without busy-waiting.
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                         uint32_t width,
                                         uint32_t height) noexcept {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = {width, height};
    actualExtent.width = std::clamp(
        actualExtent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width
    );
    actualExtent.height = std::clamp(
        actualExtent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height
    );
    return actualExtent;
}

} // namespace sidera
