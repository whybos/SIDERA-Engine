#include "renderer/vulkan/VulkanContext.h"
#include "core/Log.h"
#include "core/Window.h"

namespace sidera {

namespace {
constexpr bool kEnableValidationInBuild =
#if defined(SIDERA_ENABLE_VALIDATION)
    true;
#else
    false;
#endif
} // namespace

VulkanContext::VulkanContext(const Window& window)
    : m_instance("SIDERA Engine", window.getRequiredInstanceExtensions(), kEnableValidationInBuild) {
    m_surface = window.createSurface(m_instance.getHandle());
    SIDERA_LOG_INFO("Vulkan", "Window surface created.");

    try {
        m_device = std::make_unique<VulkanDevice>(m_instance.getHandle(), m_surface);
    } catch (...) {
        if (m_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(m_instance.getHandle(), m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }
        throw;
    }

    SIDERA_LOG_INFO("Vulkan", "VulkanContext initialized successfully.");
}

VulkanContext::~VulkanContext() {
    SIDERA_LOG_INFO("Vulkan", "Shutting down VulkanContext...");

    if (m_device) {
        m_device->waitIdle();
        m_device.reset();
    }

    if (m_surface != VK_NULL_HANDLE) {
        SIDERA_LOG_INFO("Vulkan", "Destroying Vulkan window surface...");
        vkDestroySurfaceKHR(m_instance.getHandle(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}

void VulkanContext::waitIdle() const {
    if (m_device) {
        m_device->waitIdle();
    }
}

} // namespace sidera
