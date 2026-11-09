#pragma once

#include "renderer/vulkan/GraphicsPipeline.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanFrameSync.h"
#include "renderer/vulkan/VulkanSwapchain.h"

#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>

namespace sidera {

class Window;
class VulkanContext;

class Renderer {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    Renderer(const VulkanContext& context, const Window& window, const glm::vec4& clearColor);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void drawFrame(Window& window);

    void setClearColor(const glm::vec4& color) noexcept { m_clearColor = color; }
    [[nodiscard]] const glm::vec4& getClearColor() const noexcept { return m_clearColor; }
    [[nodiscard]] uint64_t getRenderedFrameCount() const noexcept { return m_renderedFrames; }
    [[nodiscard]] const VulkanSwapchain& getSwapchain() const noexcept { return *m_swapchain; }
    [[nodiscard]] const GraphicsPipeline& getPipeline() const noexcept { return *m_pipeline; }

private:
    void createCommandPoolsAndBuffers();
    void destroyCommandPools() noexcept;
    void createGeometryBuffers();
    void destroyGeometryBuffers() noexcept;

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) const;
    void submitCommandBuffer(VkCommandBuffer cmd,
                             VkSemaphore waitSemaphore,
                             VkSemaphore signalSemaphore,
                             VkFence fence) const;
    void recreateSwapchain(Window& window);

    const VulkanContext& m_context;
    std::unique_ptr<VulkanSwapchain> m_swapchain;
    std::unique_ptr<VulkanFrameSync> m_frameSync;
    std::unique_ptr<GraphicsPipeline> m_pipeline;
    std::unique_ptr<VulkanBuffer> m_vertexBuffer;
    std::unique_ptr<VulkanBuffer> m_indexBuffer;
    uint32_t m_indexCount = 0;

    std::array<VkCommandPool, kMaxFramesInFlight> m_commandPools{};
    std::array<VkCommandBuffer, kMaxFramesInFlight> m_commandBuffers{};

    uint32_t m_currentFrame = 0;
    uint64_t m_renderedFrames = 0;
    glm::vec4 m_clearColor{0.04f, 0.08f, 0.16f, 1.0f};
};

} // namespace sidera
