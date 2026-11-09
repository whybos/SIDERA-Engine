#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace sidera {

class VulkanFrameSync {
public:
    VulkanFrameSync(VkDevice device, uint32_t framesInFlight, uint32_t swapchainImageCount);
    ~VulkanFrameSync();

    VulkanFrameSync(const VulkanFrameSync&) = delete;
    VulkanFrameSync& operator=(const VulkanFrameSync&) = delete;
    VulkanFrameSync(VulkanFrameSync&&) = delete;
    VulkanFrameSync& operator=(VulkanFrameSync&&) = delete;

    void waitForFrameFence(uint32_t frameIndex) const;
    void resetFrameFence(uint32_t frameIndex) const;
    void resizeRenderFinishedSemaphores(uint32_t swapchainImageCount);

    [[nodiscard]] VkSemaphore getImageAvailableSemaphore(uint32_t frameIndex) const noexcept {
        return m_imageAvailableSemaphores[frameIndex];
    }

    [[nodiscard]] VkFence getInFlightFence(uint32_t frameIndex) const noexcept {
        return m_inFlightFences[frameIndex];
    }

    [[nodiscard]] VkSemaphore getRenderFinishedSemaphore(uint32_t imageIndex) const noexcept {
        return m_renderFinishedSemaphores[imageIndex];
    }

    [[nodiscard]] uint32_t getFramesInFlight() const noexcept {
        return static_cast<uint32_t>(m_inFlightFences.size());
    }

private:
    void createRenderFinishedSemaphores(uint32_t count);
    void destroyRenderFinishedSemaphores() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkFence> m_inFlightFences;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
};

} // namespace sidera
