#include "renderer/vulkan/VulkanFrameSync.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanCheck.h"

#include <limits>

namespace sidera {

VulkanFrameSync::VulkanFrameSync(VkDevice device, uint32_t framesInFlight, uint32_t swapchainImageCount)
    : m_device(device) {
    m_imageAvailableSemaphores.resize(framesInFlight, VK_NULL_HANDLE);
    m_inFlightFences.resize(framesInFlight, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < framesInFlight; ++i) {
        VK_CHECK(
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]),
            "Failed to create imageAvailable semaphore"
        );
        VK_CHECK(
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]),
            "Failed to create inFlight fence"
        );
    }

    createRenderFinishedSemaphores(swapchainImageCount);

    SIDERA_LOG_INFO(
        "FrameSync",
        "Created synchronization primitives ({} frames in flight, {} per-image present semaphores).",
        framesInFlight,
        swapchainImageCount
    );
}

VulkanFrameSync::~VulkanFrameSync() {
    SIDERA_LOG_INFO("FrameSync", "Destroying frame synchronization primitives...");

    destroyRenderFinishedSemaphores();

    for (VkSemaphore sem : m_imageAvailableSemaphores) {
        if (sem != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, sem, nullptr);
        }
    }
    m_imageAvailableSemaphores.clear();

    for (VkFence fence : m_inFlightFences) {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, fence, nullptr);
        }
    }
    m_inFlightFences.clear();
}

void VulkanFrameSync::waitForFrameFence(uint32_t frameIndex) const {
    VK_CHECK(
        vkWaitForFences(
            m_device,
            1,
            &m_inFlightFences[frameIndex],
            VK_TRUE,
            std::numeric_limits<uint64_t>::max()
        ),
        "Failed waiting for in-flight frame fence"
    );
}

void VulkanFrameSync::resetFrameFence(uint32_t frameIndex) const {
    VK_CHECK(
        vkResetFences(m_device, 1, &m_inFlightFences[frameIndex]),
        "Failed resetting in-flight frame fence"
    );
}

void VulkanFrameSync::resizeRenderFinishedSemaphores(uint32_t swapchainImageCount) {
    if (m_renderFinishedSemaphores.size() == swapchainImageCount) {
        return;
    }
    destroyRenderFinishedSemaphores();
    createRenderFinishedSemaphores(swapchainImageCount);
}

void VulkanFrameSync::createRenderFinishedSemaphores(uint32_t count) {
    m_renderFinishedSemaphores.resize(count, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < count; ++i) {
        VK_CHECK(
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]),
            "Failed to create renderFinished semaphore"
        );
    }
}

void VulkanFrameSync::destroyRenderFinishedSemaphores() noexcept {
    for (VkSemaphore sem : m_renderFinishedSemaphores) {
        if (sem != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, sem, nullptr);
        }
    }
    m_renderFinishedSemaphores.clear();
}

} // namespace sidera
