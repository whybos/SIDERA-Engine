#pragma once

#include "renderer/Camera.h"
#include "renderer/Mesh.h"
#include "renderer/vulkan/GraphicsPipeline.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanFrameSync.h"
#include "renderer/vulkan/VulkanSwapchain.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sidera {

class Window;
class VulkanContext;

struct RenderObject {
    const Mesh* mesh = nullptr;
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec3 rotationAxis{0.0f, 1.0f, 0.0f};
    float rotationRadians = 0.0f;
    float angularSpeed = 0.0f;

    [[nodiscard]] glm::mat4 computeModelMatrix() const noexcept;
};

class Renderer {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    Renderer(const VulkanContext& context, const Window& window, const glm::vec4& clearColor);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void updateScene(float deltaTime) noexcept;
    void drawFrame(Window& window);

    [[nodiscard]] Camera& getCamera() noexcept { return m_camera; }
    [[nodiscard]] const Camera& getCamera() const noexcept { return m_camera; }

    void setClearColor(const glm::vec4& color) noexcept { m_clearColor = color; }
    [[nodiscard]] const glm::vec4& getClearColor() const noexcept { return m_clearColor; }
    [[nodiscard]] uint64_t getRenderedFrameCount() const noexcept { return m_renderedFrames; }
    [[nodiscard]] const VulkanSwapchain& getSwapchain() const noexcept { return *m_swapchain; }
    [[nodiscard]] const GraphicsPipeline& getPipeline() const noexcept { return *m_pipeline; }

private:
    void createCameraDescriptorResources();
    void destroyCameraDescriptorResources() noexcept;
    void createCommandPoolsAndBuffers();
    void destroyCommandPools() noexcept;
    void createSceneMeshes();
    void destroySceneMeshes() noexcept;

    void updateCameraUniformBuffer(uint32_t frameIndex);
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) const;
    void submitCommandBuffer(VkCommandBuffer cmd,
                             VkSemaphore waitSemaphore,
                             VkSemaphore signalSemaphore,
                             VkFence fence) const;
    void recreateSwapchain(Window& window);

    const VulkanContext& m_context;
    Camera m_camera;

    std::unique_ptr<VulkanSwapchain> m_swapchain;
    std::unique_ptr<VulkanFrameSync> m_frameSync;

    VkDescriptorSetLayout m_cameraDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_cameraDescriptorSets{};
    std::array<std::unique_ptr<VulkanBuffer>, kMaxFramesInFlight> m_cameraUniformBuffers;

    std::unique_ptr<GraphicsPipeline> m_pipeline;

    std::unique_ptr<Mesh> m_groundMesh;
    std::unique_ptr<Mesh> m_cubeMesh;
    std::unique_ptr<Mesh> m_pyramidMesh;
    std::vector<RenderObject> m_renderObjects;

    std::array<VkCommandPool, kMaxFramesInFlight> m_commandPools{};
    std::array<VkCommandBuffer, kMaxFramesInFlight> m_commandBuffers{};

    uint32_t m_currentFrame = 0;
    uint64_t m_renderedFrames = 0;
    glm::vec4 m_clearColor{0.04f, 0.08f, 0.16f, 1.0f};
};

} // namespace sidera
