#pragma once

#include "renderer/Camera.h"
#include "renderer/Mesh.h"
#include "renderer/vulkan/GraphicsPipeline.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanDepthBuffer.h"
#include "renderer/vulkan/VulkanShadowMap.h"
#include "renderer/vulkan/VulkanFrameSync.h"
#include "renderer/vulkan/VulkanSwapchain.h"
#include "renderer/vulkan/VulkanTexture.h"
#include "renderer/vulkan/VulkanCubemap.h"
#include "renderer/IBLBuilder.h"

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

struct ShadowPushConstants {
    glm::mat4 lightSpaceMatrix;
    glm::mat4 model;
};

struct RenderObject {
    const Mesh* mesh = nullptr;
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec3 rotationAxis{0.0f, 1.0f, 0.0f};
    float rotationRadians = 0.0f;
    float angularSpeed = 0.0f;

    // PBR material
    glm::vec3 albedo{1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    float textureStrength = 0.0f;

    [[nodiscard]] glm::mat4 computeModelMatrix() const noexcept;
};

// Must match the SceneUBO block in triangle.vert / triangle.frag (std140)
struct alignas(16) SceneUniformData {
    CameraUniformData camera;
    glm::mat4 lightSpaceMatrix{1.0f};
    glm::vec4 cameraPos{0.0f};
    glm::vec4 sunDirection{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 sunColor{1.0f};
    std::array<glm::vec4, 2> pointPosIntensity{};
    std::array<glm::vec4, 2> pointColor{};
    glm::vec4 shadowParams{1.0f, 0.0025f, 3.0f, 0.0f};
    glm::vec4 screenParams{1920.0f, 1080.0f, 0.0f, 0.0f};
};

class Renderer {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    Renderer(const VulkanContext& context,
             const Window& window,
             const glm::vec4& clearColor,
             bool shadowsEnabled = true,
             uint32_t shadowResolution = 2048,
             uint32_t shadowDebugMode = 0);
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
    [[nodiscard]] const VulkanDepthBuffer& getDepthBuffer() const noexcept { return *m_depthBuffer; }
    [[nodiscard]] const VulkanShadowMap& getShadowMap() const noexcept { return *m_shadowMap; }
    [[nodiscard]] const GraphicsPipeline& getPipeline() const noexcept { return *m_pipeline; }
    [[nodiscard]] uint32_t getDrawCallCount() const noexcept { return static_cast<uint32_t>(m_renderObjects.size()); }
    [[nodiscard]] uint32_t getShadowDrawCallCount() const noexcept { return m_shadowDrawCalls; }
    [[nodiscard]] uint32_t getTriangleCount() const noexcept;

    void setStressTestMode(bool enabled);
    void toggleStressTestMode();
    [[nodiscard]] bool isStressTestMode() const noexcept { return m_stressTestMode; }

    void setShadowsEnabled(bool enabled) noexcept { m_shadowsEnabled = enabled; }
    void toggleShadows() noexcept { m_shadowsEnabled = !m_shadowsEnabled; }
    [[nodiscard]] bool isShadowsEnabled() const noexcept { return m_shadowsEnabled; }

    void setShadowResolution(uint32_t resolution);
    [[nodiscard]] uint32_t getShadowResolution() const noexcept { return m_shadowMap ? m_shadowMap->resolution() : 0; }

    void setShadowDebugMode(uint32_t mode) noexcept { m_shadowDebugMode = mode; }
    void cycleShadowDebugMode() noexcept;
    [[nodiscard]] uint32_t getShadowDebugMode() const noexcept { return m_shadowDebugMode; }

    [[nodiscard]] glm::mat4 computeLightSpaceMatrix() const noexcept;

private:
    void createCameraDescriptorResources();
    void updateShadowDescriptorSets();
    void destroyCameraDescriptorResources() noexcept;
    void createCommandPoolsAndBuffers();
    void destroyCommandPools() noexcept;
    void createSceneMeshes();
    void destroySceneMeshes() noexcept;

    void createShadowPipeline();
    void destroyShadowPipeline() noexcept;

    void updateCameraUniformBuffer(uint32_t frameIndex);
    void recordShadowPass(VkCommandBuffer cmd);
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
    void submitCommandBuffer(VkCommandBuffer cmd,
                             VkSemaphore waitSemaphore,
                             VkSemaphore signalSemaphore,
                             VkFence fence) const;
    void recreateSwapchain(Window& window);

    const VulkanContext& m_context;
    Camera m_camera;

    std::unique_ptr<VulkanSwapchain> m_swapchain;
    std::unique_ptr<VulkanDepthBuffer> m_depthBuffer;
    std::unique_ptr<VulkanShadowMap> m_shadowMap;
    std::unique_ptr<VulkanFrameSync> m_frameSync;
    std::unique_ptr<VulkanTexture> m_albedoTexture;
    IBLMaps m_iblMaps;

    VkDescriptorSetLayout m_cameraDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFramesInFlight> m_cameraDescriptorSets{};
    std::array<std::unique_ptr<VulkanBuffer>, kMaxFramesInFlight> m_cameraUniformBuffers;

    std::unique_ptr<GraphicsPipeline> m_pipeline;

    // Dedicated depth-only shadow pipeline
    VkPipelineLayout m_shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_shadowPipeline = VK_NULL_HANDLE;

    bool m_stressTestMode = false;
    bool m_shadowsEnabled = true;
    uint32_t m_shadowDebugMode = 0; // 0=off, 1=PIP thumbnail, 2=fullscreen depth, 3=shadow mask
    float m_shadowDepthBias = 0.0025f;
    uint32_t m_shadowDrawCalls = 0;

    std::unique_ptr<Mesh> m_groundMesh;
    std::unique_ptr<Mesh> m_cubeMesh;
    std::unique_ptr<Mesh> m_pyramidMesh;
    std::unique_ptr<Mesh> m_sphereMesh;
    std::unique_ptr<Mesh> m_icosphereMesh;
    std::unique_ptr<Mesh> m_gearMesh;
    std::unique_ptr<Mesh> m_torusMesh;
    std::unique_ptr<Mesh> m_torusKnotMesh;
    std::unique_ptr<Mesh> m_turbineMesh;
    std::vector<RenderObject> m_renderObjects;

    std::array<VkCommandPool, kMaxFramesInFlight> m_commandPools{};
    std::array<VkCommandBuffer, kMaxFramesInFlight> m_commandBuffers{};

    uint32_t m_currentFrame = 0;
    uint64_t m_renderedFrames = 0;
    glm::vec4 m_clearColor{0.04f, 0.08f, 0.16f, 1.0f};
};

} // namespace sidera
