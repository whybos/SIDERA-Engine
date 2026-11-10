#include "renderer/Renderer.h"
#include "core/Log.h"
#include "core/Window.h"
#include "renderer/ProceduralMeshes.h"
#include "renderer/Vertex.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanContext.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanShader.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>

namespace sidera {

glm::mat4 RenderObject::computeModelMatrix() const noexcept {
    glm::mat4 model{1.0f};
    model = glm::translate(model, position);
    if (rotationRadians != 0.0f) {
        model = glm::rotate(model, rotationRadians, glm::normalize(rotationAxis));
    }
    model = glm::scale(model, scale);
    return model;
}

AABB RenderObject::computeWorldAABB() const noexcept {
    if (!mesh) {
        return AABB{.min = position, .max = position};
    }
    return mesh->getLocalAABB().transform(computeModelMatrix());
}

BoundingSphere RenderObject::computeWorldBoundingSphere() const noexcept {
    if (!mesh) {
        return BoundingSphere{.center = position, .radius = 0.0f};
    }
    return mesh->getLocalBoundingSphere().transform(computeModelMatrix());
}

namespace {
constexpr const char* kVertShaderPath = "shaders/triangle.vert.spv";
constexpr const char* kFragShaderPath = "shaders/triangle.frag.spv";
constexpr const char* kShadowVertShaderPath = "shaders/shadow_depth.vert.spv";
constexpr const char* kIndirectVertShaderPath = "shaders/triangle_indirect.vert.spv";
constexpr const char* kShadowIndirectVertShaderPath = "shaders/shadow_depth_indirect.vert.spv";

void transitionImageLayout(VkCommandBuffer cmd,
                           VkImage image,
                           VkImageAspectFlags aspectMask,
                           VkImageLayout oldLayout,
                           VkImageLayout newLayout,
                           VkPipelineStageFlags2 srcStageMask,
                           VkAccessFlags2 srcAccessMask,
                           VkPipelineStageFlags2 dstStageMask,
                           VkAccessFlags2 dstAccessMask) noexcept {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStageMask;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstStageMask = dstStageMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}
} // namespace

Renderer::Renderer(const VulkanContext& context,
                   const Window& window,
                   const glm::vec4& clearColor,
                   bool shadowsEnabled,
                   uint32_t shadowResolution,
                   uint32_t cascadeCount,
                   uint32_t shadowDebugMode,
                   RenderMode renderMode)
    : m_context(context)
    , m_clearColor(clearColor)
    , m_shadowsEnabled(shadowsEnabled)
    , m_cascadeCount(std::clamp(cascadeCount, 1u, 4u))
    , m_shadowDebugMode(shadowDebugMode)
    , m_renderMode(renderMode) {
    const auto fbSize = window.getFramebufferSize();

    m_swapchain = std::make_unique<VulkanSwapchain>(
        m_context.getDevice(),
        m_context.getSurface(),
        fbSize.x,
        fbSize.y,
        true
    );

    const VkExtent2D swapExtent = m_swapchain->getExtent();

    m_depthBuffer = std::make_unique<VulkanDepthBuffer>(
        m_context.getDevice(),
        swapExtent.width,
        swapExtent.height
    );

    m_shadowMap = std::make_unique<VulkanShadowMap>(
        m_context.getDevice(),
        shadowResolution,
        m_cascadeCount
    );

    m_camera.setPosition({0.0f, 2.30f, 4.60f});
    m_camera.setOrientation(-90.0f, -18.0f);
    m_camera.setViewportSize(swapExtent.width, swapExtent.height);

    m_frameSync = std::make_unique<VulkanFrameSync>(
        m_context.getDevice().getLogicalDevice(),
        kMaxFramesInFlight,
        m_swapchain->getImageCount()
    );

    try {
        createCommandPoolsAndBuffers();
        createTimestampResources();
        m_textureManager = std::make_unique<TextureManager>(m_context.getDevice(), m_commandPools[0]);
        m_albedoTexture = m_textureManager->getProceduralUvGrid();
        m_viewportManager.updateExtents(swapExtent.width, swapExtent.height);
        m_iblMaps = IBLBuilder::build(m_context.getDevice(), m_commandPools[0]);
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            m_hizBuffers[i] = std::make_unique<VulkanHiZBuffer>(m_context.getDevice(), 1024, 512);
        }
        createGpuScene(); // optional; leaves GPU-driven mode unavailable on failure
        createCameraDescriptorResources();

        m_pipeline = std::make_unique<GraphicsPipeline>(
            m_context.getDevice().getLogicalDevice(),
            m_swapchain->getImageFormat(),
            m_depthBuffer->format(),
            m_cameraDescriptorSetLayout,
            kVertShaderPath,
            kFragShaderPath
        );

        createShadowPipeline();
        createGpuPipelines(); // optional; leaves GPU-driven mode unavailable on failure
        createSceneMeshes();
    } catch (...) {
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            m_hizBuffers[i].reset();
            m_hizValid[i] = false;
        }
        destroySceneMeshes();
        destroyGpuResources();
        destroyShadowPipeline();
        m_pipeline.reset();
        m_pipelineWireframe.reset();
        destroyCameraDescriptorResources();
        m_iblMaps = {};
        m_albedoTexture.reset();
        m_textureManager.reset();
        m_shadowMap.reset();
        destroyTimestampResources();
        destroyCommandPools();
        throw;
    }

    if (m_renderMode == RenderMode::Gpu && !isGpuDrivenAvailable()) {
        SIDERA_LOG_WARN("Renderer", "GPU-driven rendering requested but unavailable; using the CPU-driven path.");
        m_renderMode = RenderMode::Cpu;
    }

    const auto& camPos = m_camera.getPosition();
    SIDERA_LOG_INFO(
        "Camera",
        "Initialized 3D Camera (pos=[{:.2f}, {:.2f}, {:.2f}], yaw={:.1f} deg, pitch={:.1f} deg, "
        "FOV={:.1f} deg, aspect={:.3f}).",
        camPos.x,
        camPos.y,
        camPos.z,
        m_camera.getYawDegrees(),
        m_camera.getPitchDegrees(),
        m_camera.getFovDegrees(),
        m_camera.getAspectRatio()
    );

    SIDERA_LOG_INFO(
        "Renderer",
        "Renderer initialized ({} 3D scene objects, Depth: ENABLED, PBR: ENABLED, CSM: {} cascades ({}x{} px), FramesInFlight: {}).",
        m_renderObjects.size(),
        m_cascadeCount,
        m_shadowMap->resolution(),
        m_shadowMap->resolution(),
        kMaxFramesInFlight
    );
    SIDERA_LOG_INFO(
        "Renderer",
        "Rendering mode: {} (GPU-driven path {}).",
        m_renderMode == RenderMode::Gpu ? "GPU-DRIVEN (compute-generated indirect draws)" : "CPU-DRIVEN (direct draws)",
        isGpuDrivenAvailable() ? "available" : "unavailable"
    );
}

Renderer::~Renderer() {
    SIDERA_LOG_INFO("Renderer", "Shutting down Renderer (total rendered frames: {})...", m_renderedFrames);

    m_context.waitIdle();

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_hizBuffers[i].reset();
        m_hizValid[i] = false;
    }
    destroySceneMeshes();
    destroyGpuResources();
    destroyShadowPipeline();
    m_pipeline.reset();
    m_pipelineWireframe.reset();
    destroyCameraDescriptorResources();
    m_iblMaps = {};
    m_albedoTexture.reset();
    m_textureManager.reset();
    m_shadowMap.reset();
    destroyTimestampResources();
    destroyCommandPools();
    m_frameSync.reset();
    m_depthBuffer.reset();
    m_swapchain.reset();
}

void Renderer::updateScene(float deltaTime) noexcept {
    if (m_instancingMode == InstancingMode::Normal) {
        for (auto& obj : m_renderObjects) {
            if (obj.angularSpeed != 0.0f) {
                obj.rotationRadians += obj.angularSpeed * deltaTime;
            }
        }
        for (const auto& batch : m_batchManager.getBatches()) {
            for (uint32_t idx = 0; idx < batch->getInstanceCount(); ++idx) {
                const uint32_t objIdx = batch->getInstances()[idx].customId;
                if (objIdx < m_renderObjects.size()) {
                    batch->updateInstanceTransform(idx, m_renderObjects[objIdx].computeModelMatrix());
                }
            }
        }
    } else {
        for (const auto& batch : m_batchManager.getBatches()) {
            for (uint32_t idx = 0; idx < batch->getInstanceCount(); ++idx) {
                auto& inst = batch->getInstances()[idx];
                if (inst.angularSpeed != 0.0f) {
                    inst.rotationRadians += inst.angularSpeed * deltaTime;
                    glm::mat4 m(1.0f);
                    m = glm::translate(m, inst.position);
                    m = glm::rotate(m, inst.rotationRadians, inst.rotationAxis);
                    batch->updateInstanceTransform(idx, m);
                }
            }
        }
    }
}

void Renderer::drawFrame(Window& window) {
    // Handle minimized window safely without busy-waiting
    if (window.isMinimized()) {
        window.waitEvents();
        return;
    }

    if (window.wasResized()) {
        recreateSwapchain(window);
        return;
    }

    // 1. Wait until the GPU has finished executing the previous frame using this slot
    m_frameSync->waitForFrameFence(m_currentFrame);
    collectGpuTimestamps(m_currentFrame);
    if (m_gpuScene) {
        m_lastCullingStats = m_gpuScene->readCullingStats(m_currentFrame);
    }

    // 2. Acquire the next image from the swapchain
    uint32_t imageIndex = 0;
    const VkSemaphore imageAvailableSemaphore = m_frameSync->getImageAvailableSemaphore(m_currentFrame);
    const VkResult acquireResult = m_swapchain->acquireNextImage(imageAvailableSemaphore, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain(window);
        return;
    }

    // 3. Only reset the fence once we know we will submit work to signal it
    m_frameSync->resetFrameFence(m_currentFrame);

    // CPU submission timing starts here (fence wait and swapchain acquire are excluded)
    const auto cpuSubmitStart = std::chrono::steady_clock::now();

    // 4. Update the current frame's camera uniform buffer (and GPU-resident object data)
    updateCameraUniformBuffer(m_currentFrame);
    if (isGpuDrivenActive()) {
        uploadGpuObjects(m_currentFrame);
    }

    // 5. Reset the frame's command pool and record dynamic rendering commands
    const VkDevice device = m_context.getDevice().getLogicalDevice();
    VK_CHECK(
        vkResetCommandPool(device, m_commandPools[m_currentFrame], 0),
        "Failed to reset frame command pool"
    );

    const VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
    recordCommandBuffer(cmd, imageIndex);

    // 6. Submit command buffer to graphics queue
    const VkSemaphore renderFinishedSemaphore = m_frameSync->getRenderFinishedSemaphore(imageIndex);
    const VkFence inFlightFence = m_frameSync->getInFlightFence(m_currentFrame);
    submitCommandBuffer(cmd, imageAvailableSemaphore, renderFinishedSemaphore, inFlightFence);

    const double cpuSubmitMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuSubmitStart).count();
    m_cpuSubmitSumMs += cpuSubmitMs;
    m_cpuSubmitMaxMs = std::max(m_cpuSubmitMaxMs, cpuSubmitMs);
    ++m_cpuSubmitSamples;

    // 7. Present the rendered image to the swapchain
    const VkResult presentResult = m_swapchain->present(imageIndex, renderFinishedSemaphore);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        window.wasResized()) {
        recreateSwapchain(window);
    }

    m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
    ++m_renderedFrames;
}

void Renderer::createCameraDescriptorResources() {
    const VkDevice device = m_context.getDevice().getLogicalDevice();

    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding irradianceLayoutBinding{};
    irradianceLayoutBinding.binding = 2;
    irradianceLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    irradianceLayoutBinding.descriptorCount = 1;
    irradianceLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding prefilteredLayoutBinding{};
    prefilteredLayoutBinding.binding = 3;
    prefilteredLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    prefilteredLayoutBinding.descriptorCount = 1;
    prefilteredLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding brdfLutLayoutBinding{};
    brdfLutLayoutBinding.binding = 4;
    brdfLutLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    brdfLutLayoutBinding.descriptorCount = 1;
    brdfLutLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowLayoutBinding{};
    shadowLayoutBinding.binding = 5;
    shadowLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowLayoutBinding.descriptorCount = 1;
    shadowLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 6: GPU-resident object data (GPU-driven path only; unused by the CPU-driven shaders)
    VkDescriptorSetLayoutBinding objectLayoutBinding{};
    objectLayoutBinding.binding = 6;
    objectLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    objectLayoutBinding.descriptorCount = 1;
    objectLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 7: Hi-Z map (for debug visualization in fragment shader)
    VkDescriptorSetLayoutBinding hizLayoutBinding{};
    hizLayoutBinding.binding = 7;
    hizLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hizLayoutBinding.descriptorCount = 1;
    hizLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 8: GPU-resident visible instance index buffer (GPU instancing)
    VkDescriptorSetLayoutBinding visibleLayoutBinding{};
    visibleLayoutBinding.binding = 8;
    visibleLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    visibleLayoutBinding.descriptorCount = 1;
    visibleLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 9: Normal Map
    VkDescriptorSetLayoutBinding normalLayoutBinding{};
    normalLayoutBinding.binding = 9;
    normalLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalLayoutBinding.descriptorCount = 1;
    normalLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 10: Metallic-Roughness Map
    VkDescriptorSetLayoutBinding metalRoughLayoutBinding{};
    metalRoughLayoutBinding.binding = 10;
    metalRoughLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    metalRoughLayoutBinding.descriptorCount = 1;
    metalRoughLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 11: Ambient Occlusion Map
    VkDescriptorSetLayoutBinding aoLayoutBinding{};
    aoLayoutBinding.binding = 11;
    aoLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    aoLayoutBinding.descriptorCount = 1;
    aoLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 12: Emissive Map
    VkDescriptorSetLayoutBinding emissiveLayoutBinding{};
    emissiveLayoutBinding.binding = 12;
    emissiveLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    emissiveLayoutBinding.descriptorCount = 1;
    emissiveLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    const std::array<VkDescriptorSetLayoutBinding, 13> bindings = {
        uboLayoutBinding,
        samplerLayoutBinding,
        irradianceLayoutBinding,
        prefilteredLayoutBinding,
        brdfLutLayoutBinding,
        shadowLayoutBinding,
        objectLayoutBinding,
        hizLayoutBinding,
        visibleLayoutBinding,
        normalLayoutBinding,
        metalRoughLayoutBinding,
        aoLayoutBinding,
        emissiveLayoutBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VK_CHECK(
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_cameraDescriptorSetLayout),
        "Failed to create camera descriptor set layout"
    );

    const std::array<VkDescriptorPoolSize, 3> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFramesInFlight},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFramesInFlight * 10},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxFramesInFlight * 4}
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = kMaxFramesInFlight;

    VK_CHECK(
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool),
        "Failed to create camera descriptor pool"
    );

    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> layouts{};
    layouts.fill(m_cameraDescriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = kMaxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    VK_CHECK(
        vkAllocateDescriptorSets(device, &allocInfo, m_cameraDescriptorSets.data()),
        "Failed to allocate camera descriptor sets"
    );

    const VkDeviceSize uboSize = sizeof(SceneUniformData);
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_cameraUniformBuffers[i] = std::make_unique<VulkanBuffer>(
            m_context.getDevice(),
            uboSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        updateCameraUniformBuffer(i);

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_cameraUniformBuffers[i]->getHandle();
        bufferInfo.offset = 0;
        bufferInfo.range = uboSize;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_albedoTexture->imageView();
        imageInfo.sampler = m_albedoTexture->sampler();

        VkDescriptorImageInfo irrInfo{};
        irrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        irrInfo.imageView = m_iblMaps.irradianceMap->imageView();
        irrInfo.sampler = m_iblMaps.irradianceMap->sampler();

        VkDescriptorImageInfo prefInfo{};
        prefInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        prefInfo.imageView = m_iblMaps.prefilteredMap->imageView();
        prefInfo.sampler = m_iblMaps.prefilteredMap->sampler();

        VkDescriptorImageInfo lutInfo{};
        lutInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lutInfo.imageView = m_iblMaps.brdfLut->imageView();
        lutInfo.sampler = m_iblMaps.brdfLut->sampler();

        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = m_shadowMap->imageView();
        shadowInfo.sampler = m_shadowMap->sampler();

        VkDescriptorImageInfo hizInfo{};
        hizInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        hizInfo.imageView = m_hizBuffers[i] ? m_hizBuffers[i]->sampledImageView() : m_albedoTexture->imageView();
        hizInfo.sampler = m_hizBuffers[i] ? m_hizBuffers[i]->sampler() : m_albedoTexture->sampler();

        VkDescriptorImageInfo normalInfo{};
        normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        normalInfo.imageView = m_textureManager->getProceduralBrickNormal()->imageView();
        normalInfo.sampler = m_textureManager->getProceduralBrickNormal()->sampler();

        VkDescriptorImageInfo metalRoughInfo{};
        metalRoughInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        metalRoughInfo.imageView = m_textureManager->getProceduralMetalRough()->imageView();
        metalRoughInfo.sampler = m_textureManager->getProceduralMetalRough()->sampler();

        VkDescriptorImageInfo aoInfo{};
        aoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        aoInfo.imageView = m_textureManager->getDefaultWhiteAo()->imageView();
        aoInfo.sampler = m_textureManager->getDefaultWhiteAo()->sampler();

        VkDescriptorImageInfo emissiveInfo{};
        emissiveInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        emissiveInfo.imageView = m_textureManager->getProceduralEmissive()->imageView();
        emissiveInfo.sampler = m_textureManager->getProceduralEmissive()->sampler();

        std::array<VkWriteDescriptorSet, 11> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_cameraDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_cameraDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &imageInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = m_cameraDescriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &irrInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = m_cameraDescriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &prefInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = m_cameraDescriptorSets[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &lutInfo;

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = m_cameraDescriptorSets[i];
        writes[5].dstBinding = 5;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[5].descriptorCount = 1;
        writes[5].pImageInfo = &shadowInfo;

        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = m_cameraDescriptorSets[i];
        writes[6].dstBinding = 7;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].descriptorCount = 1;
        writes[6].pImageInfo = &hizInfo;

        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = m_cameraDescriptorSets[i];
        writes[7].dstBinding = 9;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[7].descriptorCount = 1;
        writes[7].pImageInfo = &normalInfo;

        writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet = m_cameraDescriptorSets[i];
        writes[8].dstBinding = 10;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[8].descriptorCount = 1;
        writes[8].pImageInfo = &metalRoughInfo;

        writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[9].dstSet = m_cameraDescriptorSets[i];
        writes[9].dstBinding = 11;
        writes[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[9].descriptorCount = 1;
        writes[9].pImageInfo = &aoInfo;

        writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[10].dstSet = m_cameraDescriptorSets[i];
        writes[10].dstBinding = 12;
        writes[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[10].descriptorCount = 1;
        writes[10].pImageInfo = &emissiveInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    updateGpuDescriptors();

    SIDERA_LOG_INFO(
        "Renderer",
        "Created camera uniform buffers ({} x {} bytes) and descriptor sets with shadow map binding.",
        kMaxFramesInFlight,
        uboSize
    );
}

void Renderer::updateGpuDescriptors() {
    if (!m_gpuScene) {
        return;
    }
    const VkDevice device = m_context.getDevice().getLogicalDevice();
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkDescriptorBufferInfo objectInfo{};
        objectInfo.buffer = m_gpuScene->getObjectBuffer(i);
        objectInfo.offset = 0;
        objectInfo.range = m_gpuScene->getObjectBufferSize();

        VkDescriptorBufferInfo visibleInfo{};
        visibleInfo.buffer = m_gpuScene->getVisibleIndicesBuffer(i);
        visibleInfo.offset = 0;
        visibleInfo.range = m_gpuScene->getVisibleIndicesBufferSize();

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_cameraDescriptorSets[i];
        writes[0].dstBinding = 6;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &objectInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_cameraDescriptorSets[i];
        writes[1].dstBinding = 8;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &visibleInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void Renderer::updateShadowDescriptorSets() {
    const VkDevice device = m_context.getDevice().getLogicalDevice();

    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shadowInfo.imageView = m_shadowMap->imageView();
    shadowInfo.sampler = m_shadowMap->sampler();

    std::array<VkWriteDescriptorSet, kMaxFramesInFlight> writes{};
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_cameraDescriptorSets[i];
        writes[i].dstBinding = 5;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &shadowInfo;
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void Renderer::destroyCameraDescriptorResources() noexcept {
    const VkDevice device = m_context.getDevice().getLogicalDevice();

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (m_cameraUniformBuffers[i]) {
            SIDERA_LOG_INFO("Buffer", "Destroying Camera Uniform Buffer [{}]...", i);
            m_cameraUniformBuffers[i].reset();
        }
    }

    if (m_descriptorPool != VK_NULL_HANDLE) {
        SIDERA_LOG_INFO("Renderer", "Destroying camera descriptor pool...");
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    if (m_cameraDescriptorSetLayout != VK_NULL_HANDLE) {
        SIDERA_LOG_INFO("Renderer", "Destroying camera descriptor set layout...");
        vkDestroyDescriptorSetLayout(device, m_cameraDescriptorSetLayout, nullptr);
        m_cameraDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void Renderer::updateCascades() noexcept {
    const float nearZ = m_camera.getNearPlane();
    const float farZ = std::min(m_camera.getFarPlane(), m_shadowDistance);
    const float range = farZ - nearZ;
    const float ratio = farZ / std::max(nearZ, 0.001f);
    const float lambda = m_cascadeLambda;

    // Calculate practical split boundaries combining uniform and logarithmic
    for (uint32_t i = 0; i < m_cascadeCount; ++i) {
        const float p = static_cast<float>(i + 1) / static_cast<float>(m_cascadeCount);
        const float logDepth = nearZ * std::pow(ratio, p);
        const float uniformDepth = nearZ + range * p;
        m_cascadeSplits[i] = lambda * logDepth + (1.0f - lambda) * uniformDepth;
    }
    for (uint32_t i = m_cascadeCount; i < 4; ++i) {
        m_cascadeSplits[i] = farZ;
    }

    const glm::vec3 sunDir = glm::normalize(glm::vec3(-0.45f, 0.80f, 0.40f));
    const float tanHalfFov = std::tan(glm::radians(m_camera.getFovDegrees()) * 0.5f);
    const float aspect = m_camera.getAspectRatio();
    const glm::mat4 camInvView = glm::inverse(m_camera.getViewMatrix());

    for (uint32_t i = 0; i < m_cascadeCount; ++i) {
        const float dNear = (i == 0) ? nearZ : m_cascadeSplits[i - 1];
        const float dFar = m_cascadeSplits[i];

        const float nearH = dNear * tanHalfFov;
        const float nearW = nearH * aspect;
        const float farH = dFar * tanHalfFov;
        const float farW = farH * aspect;

        // 8 corners of sub-frustum in camera view space
        const std::array<glm::vec3, 8> cornersView = {
            glm::vec3(-nearW,  nearH, -dNear),
            glm::vec3( nearW,  nearH, -dNear),
            glm::vec3(-nearW, -nearH, -dNear),
            glm::vec3( nearW, -nearH, -dNear),
            glm::vec3(-farW,   farH, -dFar),
            glm::vec3( farW,   farH, -dFar),
            glm::vec3(-farW,  -farH, -dFar),
            glm::vec3( farW,  -farH, -dFar)
        };

        // Transform corners to world space
        glm::vec3 frustumCenter(0.0f);
        std::array<glm::vec3, 8> cornersWorld{};
        for (size_t c = 0; c < 8; ++c) {
            cornersWorld[c] = glm::vec3(camInvView * glm::vec4(cornersView[c], 1.0f));
            frustumCenter += cornersWorld[c];
        }
        frustumCenter /= 8.0f;

        // Bounding sphere radius to prevent rotation shimmering
        float sphereRadius = 0.0f;
        for (size_t c = 0; c < 8; ++c) {
            sphereRadius = std::max(sphereRadius, glm::length(cornersWorld[c] - frustumCenter));
        }
        sphereRadius = std::ceil(sphereRadius * 16.0f) / 16.0f;

        // Virtual light position pointing along sunDir towards frustum center
        const glm::vec3 lightPos = frustumCenter + sunDir * (sphereRadius * 2.5f);
        const glm::mat4 lightView = glm::lookAt(lightPos, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));

        // Texel snapping stabilization:
        // Transform center to light view space and snap to texel increments
        const glm::vec3 centerLight = glm::vec3(lightView * glm::vec4(frustumCenter, 1.0f));
        const float shadowRes = static_cast<float>(m_shadowMap ? m_shadowMap->resolution() : 2048);
        const float worldUnitsPerTexel = (2.0f * sphereRadius) / shadowRes;

        const float snappedX = std::floor(centerLight.x / worldUnitsPerTexel) * worldUnitsPerTexel;
        const float snappedY = std::floor(centerLight.y / worldUnitsPerTexel) * worldUnitsPerTexel;
        const glm::vec3 offset(snappedX - centerLight.x, snappedY - centerLight.y, 0.0f);

        // Orthographic projection matrix for cascade
        glm::mat4 lightProj = glm::ortho(
            -sphereRadius + offset.x, sphereRadius + offset.x,
            -sphereRadius + offset.y, sphereRadius + offset.y,
            1.0f, sphereRadius * 5.0f
        );
        lightProj[1][1] *= -1.0f; // Invert Y for Vulkan clip space

        m_cascadeMatrices[i] = lightProj * lightView;
    }
    for (uint32_t i = m_cascadeCount; i < 4; ++i) {
        m_cascadeMatrices[i] = m_cascadeMatrices[m_cascadeCount - 1];
    }
}

void Renderer::setShadowResolution(uint32_t resolution) {
    if (m_shadowMap && m_shadowMap->resolution() == resolution) {
        return;
    }
    m_context.waitIdle();
    m_shadowMap->recreate(resolution, m_cascadeCount);
    updateShadowDescriptorSets();
    SIDERA_LOG_INFO("Renderer", "Shadow map resolution updated to {}x{} px ({} cascades).",
                    resolution, resolution, m_cascadeCount);
}

void Renderer::setCascadeCount(uint32_t count) {
    count = std::clamp(count, 1u, 4u);
    if (m_cascadeCount == count) {
        return;
    }
    m_context.waitIdle();
    m_cascadeCount = count;
    m_shadowMap->recreate(m_shadowMap->resolution(), m_cascadeCount);
    updateShadowDescriptorSets();
    SIDERA_LOG_INFO("Renderer", "Shadow cascade count updated to {}.", m_cascadeCount);
}

void Renderer::cycleShadowDebugMode() noexcept {
    m_shadowDebugMode = (m_shadowDebugMode + 1) % 5;
}

void Renderer::setFrustumCullingEnabled(bool enabled) noexcept {
    m_frustumCullingEnabled = enabled;
    SIDERA_LOG_INFO("Renderer", "Frustum culling: {}", enabled ? "ENABLED" : "DISABLED");
}

void Renderer::toggleFrustumCulling() noexcept {
    setFrustumCullingEnabled(!m_frustumCullingEnabled);
}

void Renderer::setOcclusionCullingEnabled(bool enabled) noexcept {
    m_occlusionCullingEnabled = enabled;
    SIDERA_LOG_INFO("Renderer", "Occlusion culling: {}", enabled ? "ENABLED" : "DISABLED");
}

void Renderer::toggleOcclusionCulling() noexcept {
    setOcclusionCullingEnabled(!m_occlusionCullingEnabled);
}

void Renderer::cycleCullingDebugMode() noexcept {
    m_cullingDebugMode = (m_cullingDebugMode + 1) % 3;
    const char* names[] = {"OFF", "FULLSCREEN HI-Z", "PIP HI-Z"};
    SIDERA_LOG_INFO("Renderer", "Hi-Z debug view: {}", names[m_cullingDebugMode]);
}

void Renderer::createShadowPipeline() {
    createShadowPipelineVariant(
        kShadowVertShaderPath,
        VK_NULL_HANDLE,
        sizeof(ShadowPushConstants),
        m_shadowPipelineLayout,
        m_shadowPipeline
    );
}

void Renderer::createShadowPipelineVariant(const char* vertSpvPath,
                                           VkDescriptorSetLayout setLayout,
                                           uint32_t pushConstantSize,
                                           VkPipelineLayout& outLayout,
                                           VkPipeline& outPipeline) {
    const VkDevice device = m_context.getDevice().getLogicalDevice();

    VulkanShader vertShader(device, vertSpvPath);

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShader.getHandle();
    vertStageInfo.pName = "main";

    const auto bindingDescription = Vertex::getBindingDescription();
    const auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    const std::array<VkDynamicState, 3> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;
    colorBlending.pAttachments = nullptr;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = pushConstantSize;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = (setLayout != VK_NULL_HANDLE) ? 1 : 0;
    pipelineLayoutInfo.pSetLayouts = (setLayout != VK_NULL_HANDLE) ? &setLayout : nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &outLayout),
        "Failed to create shadow pipeline layout"
    );

    const VkFormat depthFormat = m_shadowMap->format();
    VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
    pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineRenderingInfo.colorAttachmentCount = 0;
    pipelineRenderingInfo.pColorAttachmentFormats = nullptr;
    pipelineRenderingInfo.depthAttachmentFormat = depthFormat;
    pipelineRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &pipelineRenderingInfo;
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &vertStageInfo;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = outLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;

    VK_CHECK(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &outPipeline),
        "Failed to create shadow graphics pipeline"
    );

    SIDERA_LOG_INFO("Pipeline", "Created depth-only shadow graphics pipeline ('{}').", vertSpvPath);
}

void Renderer::destroyShadowPipeline() noexcept {
    const VkDevice device = m_context.getDevice().getLogicalDevice();
    if (device == VK_NULL_HANDLE) {
        return;
    }

    if (m_shadowPipeline != VK_NULL_HANDLE) {
        SIDERA_LOG_INFO("Pipeline", "Destroying shadow graphics pipeline...");
        vkDestroyPipeline(device, m_shadowPipeline, nullptr);
        m_shadowPipeline = VK_NULL_HANDLE;
    }

    if (m_shadowPipelineLayout != VK_NULL_HANDLE) {
        SIDERA_LOG_INFO("Pipeline", "Destroying shadow pipeline layout...");
        vkDestroyPipelineLayout(device, m_shadowPipelineLayout, nullptr);
        m_shadowPipelineLayout = VK_NULL_HANDLE;
    }
}

void Renderer::createGpuScene() {
    const VulkanDevice& dev = m_context.getDevice();
    if (!GpuDrivenScene::isSupported(dev)) {
        SIDERA_LOG_WARN(
            "GpuDriven",
            "GPU-driven rendering unavailable: drawIndirectFirstInstance is not supported by this device."
        );
        return;
    }

    try {
        m_gpuScene = std::make_unique<GpuDrivenScene>(dev, kMaxFramesInFlight);
        m_gpuScene->setOnBuffersReallocated([this]() {
            updateGpuDescriptors();
        });
    } catch (const std::exception& e) {
        SIDERA_LOG_ERROR("GpuDriven", "GPU-driven scene initialization failed: {}", e.what());
        m_gpuScene.reset();
    }
}

void Renderer::createGpuPipelines() {
    if (!m_gpuScene) {
        return;
    }

    try {
        m_pipelineIndirect = std::make_unique<GraphicsPipeline>(
            m_context.getDevice().getLogicalDevice(),
            m_swapchain->getImageFormat(),
            m_depthBuffer->format(),
            m_cameraDescriptorSetLayout,
            kIndirectVertShaderPath,
            kFragShaderPath,
            VK_POLYGON_MODE_FILL,
            VK_CULL_MODE_BACK_BIT
        );

        if (m_context.getDevice().isFillModeNonSolidSupported()) {
            m_pipelineWireframe = std::make_unique<GraphicsPipeline>(
                m_context.getDevice().getLogicalDevice(),
                m_swapchain->getImageFormat(),
                m_depthBuffer->format(),
                m_cameraDescriptorSetLayout,
                kIndirectVertShaderPath,
                kFragShaderPath,
                VK_POLYGON_MODE_LINE,
                VK_CULL_MODE_NONE
            );
            SIDERA_LOG_INFO("Pipeline", "Created dedicated wireframe graphics pipeline (VK_POLYGON_MODE_LINE).");
        }

        createShadowPipelineVariant(
            kShadowIndirectVertShaderPath,
            m_cameraDescriptorSetLayout,
            sizeof(uint32_t), // cascade index
            m_shadowIndirectLayout,
            m_shadowIndirectPipeline
        );
    } catch (const std::exception& e) {
        SIDERA_LOG_ERROR("GpuDriven", "GPU-driven pipeline creation failed: {}", e.what());
        destroyGpuResources();
    }
}

void Renderer::destroyGpuResources() noexcept {
    const VkDevice device = m_context.getDevice().getLogicalDevice();
    if (device == VK_NULL_HANDLE) {
        return;
    }

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_hizBuffers[i].reset();
        m_hizValid[i] = false;
    }

    m_pipelineIndirect.reset();
    m_pipelineWireframe.reset();

    if (m_shadowIndirectPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_shadowIndirectPipeline, nullptr);
        m_shadowIndirectPipeline = VK_NULL_HANDLE;
    }
    if (m_shadowIndirectLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_shadowIndirectLayout, nullptr);
        m_shadowIndirectLayout = VK_NULL_HANDLE;
    }

    m_gpuScene.reset();
}

void Renderer::rebuildGpuScene() {
    if (!m_gpuScene) {
        return;
    }

    try {
        m_gpuScene->setSceneBatches(m_batchManager);
        updateGpuDescriptors();
        m_commandsGenerated = m_gpuScene->getCommandsGenerated();
    } catch (const std::exception& e) {
        // Callers guarantee the GPU is idle here, so the GPU-driven resources can be released.
        SIDERA_LOG_ERROR("GpuDriven", "GPU scene build failed ({}); falling back to the CPU-driven path.", e.what());
        destroyGpuResources();
        m_renderMode = RenderMode::Cpu;
    }
}

void Renderer::uploadGpuObjects(uint32_t frameIndex) {
    m_batchManager.populateGpuData(m_gpuInstances, m_gpuBatches);
    m_gpuScene->uploadInstances(frameIndex, m_gpuInstances);
    m_gpuScene->resetMainCommands(frameIndex);
}

void Renderer::setInstancingMode(InstancingMode mode, uint32_t customCount) {
    if (mode == m_instancingMode && (mode != InstancingMode::Custom || customCount == m_customInstanceCount)) {
        return;
    }
    m_context.waitIdle();
    m_instancingMode = mode;
    m_customInstanceCount = customCount;
    destroySceneMeshes();
    createSceneMeshes();

    const char* modeName = "NORMAL";
    switch (m_instancingMode) {
        case InstancingMode::Count100: modeName = "100 INSTANCES"; break;
        case InstancingMode::Count1000: modeName = "1,000 INSTANCES"; break;
        case InstancingMode::Count10000: modeName = "10,000 INSTANCES"; break;
        case InstancingMode::Custom: modeName = "CUSTOM INSTANCES"; break;
        default: break;
    }

    SIDERA_LOG_INFO(
        "Renderer",
        "Instancing mode set to: {} ({} total instances across {} batches, {} triangles).",
        modeName,
        getTotalInstanceCount(),
        getInstanceBatchCount(),
        getTriangleCount()
    );
}

void Renderer::cycleInstancingMode() {
    const uint32_t current = static_cast<uint32_t>(m_instancingMode);
    const uint32_t next = (current + 1) % 4; // Cycles Normal -> 100 -> 1000 -> 10000 -> Normal
    setInstancingMode(static_cast<InstancingMode>(next));
}

void Renderer::setRenderMode(RenderMode mode) {
    if (mode == RenderMode::Gpu && !isGpuDrivenAvailable()) {
        SIDERA_LOG_WARN("Renderer", "GPU-driven rendering is unavailable on this device; staying on the CPU-driven path.");
        mode = RenderMode::Cpu;
    }
    if (mode == m_renderMode) {
        return;
    }
    m_renderMode = mode;
    SIDERA_LOG_INFO(
        "Renderer",
        "Rendering mode switched to {} ({} objects, {} indirect commands generated per frame).",
        mode == RenderMode::Gpu ? "GPU-DRIVEN" : "CPU-DRIVEN",
        m_renderObjects.size(),
        mode == RenderMode::Gpu ? m_commandsGenerated : 0u
    );
}

void Renderer::toggleRenderMode() {
    setRenderMode(m_renderMode == RenderMode::Gpu ? RenderMode::Cpu : RenderMode::Gpu);
}

RendererTimings Renderer::consumeTimings() noexcept {
    RendererTimings t{};
    if (m_cpuSubmitSamples > 0) {
        t.cpuSamples = m_cpuSubmitSamples;
        t.cpuSubmitAvgMs = static_cast<float>(m_cpuSubmitSumMs / m_cpuSubmitSamples);
        t.cpuSubmitMaxMs = static_cast<float>(m_cpuSubmitMaxMs);
    }
    if (m_gpuSamples > 0) {
        t.gpuSamples = m_gpuSamples;
        t.gpuTotalAvgMs = static_cast<float>(m_gpuSumMs[0] / m_gpuSamples);
        t.gpuGenAvgMs = static_cast<float>(m_gpuSumMs[1] / m_gpuSamples);
        t.gpuShadowAvgMs = static_cast<float>(m_gpuSumMs[2] / m_gpuSamples);
        t.gpuMainAvgMs = static_cast<float>(m_gpuSumMs[3] / m_gpuSamples);
    }
    m_cpuSubmitSumMs = 0.0;
    m_cpuSubmitMaxMs = 0.0;
    m_cpuSubmitSamples = 0;
    for (double& v : m_gpuSumMs) {
        v = 0.0;
    }
    m_gpuSamples = 0;
    return t;
}

void Renderer::createTimestampResources() {
    const VulkanDevice& dev = m_context.getDevice();
    const uint32_t family = dev.getQueueFamilies().graphicsFamily.value();

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev.getPhysicalDevice(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(dev.getPhysicalDevice(), &familyCount, families.data());

    const uint32_t validBits = (family < familyCount) ? families[family].timestampValidBits : 0;
    const float period = dev.getProperties().limits.timestampPeriod;
    if (validBits == 0 || period <= 0.0f) {
        SIDERA_LOG_INFO("Renderer", "GPU timestamp queries unsupported on the graphics queue; GPU timings disabled.");
        return;
    }

    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = kMaxFramesInFlight * 4;
    VK_CHECK(
        vkCreateQueryPool(dev.getLogicalDevice(), &poolInfo, nullptr, &m_timestampPool),
        "Failed to create timestamp query pool"
    );
    m_timestampPeriodNs = period;
    m_timestampValidBits = validBits;
    SIDERA_LOG_INFO(
        "Renderer",
        "GPU timestamp queries enabled ({} valid bits, {:.2f} ns/tick).",
        validBits,
        period
    );
}

void Renderer::destroyTimestampResources() noexcept {
    const VkDevice device = m_context.getDevice().getLogicalDevice();
    if (device != VK_NULL_HANDLE && m_timestampPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device, m_timestampPool, nullptr);
    }
    m_timestampPool = VK_NULL_HANDLE;
}

void Renderer::collectGpuTimestamps(uint32_t frameIndex) {
    // Called after waiting for this frame slot's fence, so results from its previous use are ready.
    if (m_timestampPool == VK_NULL_HANDLE || !m_timestampValid[frameIndex]) {
        return;
    }

    std::array<uint64_t, 4> ts{};
    const VkResult result = vkGetQueryPoolResults(
        m_context.getDevice().getLogicalDevice(),
        m_timestampPool,
        frameIndex * 4,
        4,
        sizeof(ts),
        ts.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT
    );
    if (result != VK_SUCCESS) {
        return;
    }
    m_timestampValid[frameIndex] = false; // consumed; re-armed when this slot is recorded again

    const uint64_t mask = (m_timestampValidBits >= 64) ? ~0ull : ((1ull << m_timestampValidBits) - 1ull);
    const auto toMs = [&](uint64_t begin, uint64_t end) {
        return static_cast<double>((end - begin) & mask) * static_cast<double>(m_timestampPeriodNs) * 1e-6;
    };

    m_gpuSumMs[0] += toMs(ts[0], ts[3]);
    m_gpuSumMs[1] += toMs(ts[0], ts[1]);
    m_gpuSumMs[2] += toMs(ts[1], ts[2]);
    m_gpuSumMs[3] += toMs(ts[2], ts[3]);
    ++m_gpuSamples;
}

void Renderer::updateCameraUniformBuffer(uint32_t frameIndex) {
    updateCascades();

    SceneUniformData ubo{};
    ubo.camera = m_camera.getUniformData();
    ubo.cascadeLightSpaceMatrices = m_cascadeMatrices;
    ubo.cameraPos = glm::vec4(m_camera.getPosition(), 1.0f);

    // Warm key light (directional sun) + one warm and one cool point light
    ubo.sunDirection = glm::vec4(glm::normalize(glm::vec3(-0.45f, 0.80f, 0.40f)), 0.0f);
    ubo.sunColor = glm::vec4(1.0f, 0.95f, 0.85f, 2.6f);
    ubo.pointPosIntensity[0] = glm::vec4(-1.6f, 1.1f, 1.2f, 5.5f);
    ubo.pointColor[0] = glm::vec4(1.0f, 0.65f, 0.35f, 0.0f);
    ubo.pointPosIntensity[1] = glm::vec4(1.9f, 1.0f, -0.6f, 5.5f);
    ubo.pointColor[1] = glm::vec4(0.35f, 0.60f, 1.0f, 0.0f);

    ubo.cascadeSplits = glm::vec4(
        m_cascadeSplits[0],
        m_cascadeSplits[1],
        m_cascadeSplits[2],
        m_cascadeSplits[3]
    );

    uint32_t activeDebugMode = m_shadowDebugMode;
    if (m_cullingDebugMode == 1) {
        activeDebugMode = 5; // fullscreen Hi-Z
    } else if (m_cullingDebugMode == 2) {
        activeDebugMode = 6; // PiP Hi-Z
    }

    ubo.shadowParams = glm::vec4(
        m_shadowsEnabled ? 1.0f : 0.0f,
        m_shadowDepthBias,
        3.0f,
        static_cast<float>(activeDebugMode)
    );

    ubo.cascadeParams = glm::vec4(
        static_cast<float>(m_cascadeCount),
        m_cascadeBlendFraction,
        m_cascadeLambda,
        m_shadowDistance
    );

    const auto extent = m_swapchain->getExtent();
    ubo.screenParams = glm::vec4(
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        0.0f,
        0.0f
    );

    m_cameraUniformBuffers[frameIndex]->uploadData(&ubo, sizeof(SceneUniformData), 0);

    if (m_gpuScene) {
        const glm::mat4 currentViewProj = m_camera.getProjectionMatrix() * m_camera.getViewMatrix();
        const Frustum frustum = Frustum::fromViewProj(currentViewProj);

        const glm::vec3 currentCamPos = m_camera.getPosition();
        const float posDelta = glm::distance(currentCamPos, m_prevCameraPos);
        const float yawDelta = std::abs(m_camera.getYawDegrees() - m_prevCameraYaw);
        const float pitchDelta = std::abs(m_camera.getPitchDegrees() - m_prevCameraPitch);

        // Temporal safeguard: invalidate Hi-Z if large camera translation or rotation
        const bool cameraTeleported = (posDelta > 0.8f) || (yawDelta > 15.0f) || (pitchDelta > 15.0f);
        if (cameraTeleported) {
            m_hizValid.fill(false);
        }

        const uint32_t prevFrameIndex = (frameIndex + kMaxFramesInFlight - 1) % kMaxFramesInFlight;
        const bool canUseHiZ = m_occlusionCullingEnabled && m_hizValid[prevFrameIndex] && !cameraTeleported && (m_renderedFrames > 2);
        m_canUseHiZ = canUseHiZ;

        CullingUniformData cullingData{};
        cullingData.viewProj = currentViewProj;
        cullingData.prevViewProj = m_prevViewProj;
        for (int p = 0; p < 6; ++p) {
            cullingData.frustumPlanes[p] = glm::vec4(frustum.planes[p].normal, frustum.planes[p].distance);
        }
        cullingData.cameraPos = glm::vec4(currentCamPos, 1.0f);

        m_gpuScene->uploadCullingUniforms(frameIndex, cullingData);

        if (m_hizBuffers[prevFrameIndex]) {
            m_gpuScene->updateHiZDescriptor(
                frameIndex,
                m_hizBuffers[prevFrameIndex]->sampledImageView(),
                m_hizBuffers[prevFrameIndex]->sampler()
            );
        }

        m_prevViewProj = currentViewProj;
        m_prevCameraPos = currentCamPos;
        m_prevCameraYaw = m_camera.getYawDegrees();
        m_prevCameraPitch = m_camera.getPitchDegrees();
    }
}

uint32_t Renderer::getTriangleCount() const noexcept {
    uint32_t triangles = 0;
    for (const auto& batch : m_batchManager.getBatches()) {
        if (batch && batch->getMesh()) {
            triangles += batch->getInstanceCount() * (batch->getMesh()->getIndexCount() / 3);
        }
    }
    return triangles;
}

void Renderer::createCommandPoolsAndBuffers() {
    const VkDevice device = m_context.getDevice().getLogicalDevice();
    const uint32_t graphicsQueueFamily = m_context.getDevice().getQueueFamilies().graphicsFamily.value();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VK_CHECK(
            vkCreateCommandPool(device, &poolInfo, nullptr, &m_commandPools[i]),
            "Failed to create Vulkan command pool"
        );

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_commandPools[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VK_CHECK(
            vkAllocateCommandBuffers(device, &allocInfo, &m_commandBuffers[i]),
            "Failed to allocate Vulkan command buffer"
        );
    }

    SIDERA_LOG_INFO("Renderer", "Created {} command pools and primary command buffers.", kMaxFramesInFlight);
}

void Renderer::destroyCommandPools() noexcept {
    const VkDevice device = m_context.getDevice().getLogicalDevice();
    SIDERA_LOG_INFO("Renderer", "Destroying command pools...");

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (m_commandPools[i] != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, m_commandPools[i], nullptr);
            m_commandPools[i] = VK_NULL_HANDLE;
            m_commandBuffers[i] = VK_NULL_HANDLE;
        }
    }
}

void Renderer::setStressTestMode(bool enabled) {
    if (m_stressTestMode == enabled) {
        return;
    }
    m_context.waitIdle();
    m_stressTestMode = enabled;
    destroySceneMeshes();
    createSceneMeshes();
    SIDERA_LOG_INFO(
        "Renderer",
        "Scene complexity updated -> Mode: {} (draw calls: {}, total triangles: {})",
        m_stressTestMode ? "STRESS TEST (>= 1,000,000 tris)" : "NORMAL HIGH-QUALITY (>= 250,000 tris)",
        getDrawCallCount(),
        getTriangleCount()
    );
}

void Renderer::toggleStressTestMode() {
    setStressTestMode(!m_stressTestMode);
}

void Renderer::createSceneMeshes() {
    const VulkanDevice& dev = m_context.getDevice();
    const VkCommandPool pool = m_commandPools[0];

    m_renderObjects.clear();

    if (!m_stressTestMode) {
        // -------------------------------------------------------------
        // NORMAL HIGH-QUALITY SCENE (>= 250,000 visible triangles)
        // -------------------------------------------------------------
        m_groundMesh = Mesh::createGroundGrid(dev, pool, 6.0f, 16);
        m_sphereMesh = Mesh::createSphere(dev, pool, 0.20f, 32, 48);
        m_cubeMesh = Mesh::createCube(dev, pool, 0.45f);
        m_pyramidMesh = Mesh::createPyramid(dev, pool, 0.45f, 0.95f);

        // A. High-resolution Icosphere (subdivisions = 5 -> 20,480 triangles each)
        m_icosphereMesh = ProceduralMeshes::createIcosphere(dev, pool, 0.38f, 5, "Icosphere_Subdiv5");

        // B. Mechanical Gear with teeth, hub, bore, and recessed web (~32,000 triangles each)
        m_gearMesh = ProceduralMeshes::createGear(dev, pool, 30, 0.65f, 0.48f, 0.18f, 0.16f, 16, "Gear_High");

        // C. High-polygon Torus (144 x 64 -> 18,432 triangles each)
        m_torusMesh = ProceduralMeshes::createTorus(dev, pool, 0.50f, 0.13f, 144, 64, "Torus_High");

        // D. Procedural Torus Knot (360 x 64 -> 46,080 triangles each)
        m_torusKnotMesh = ProceduralMeshes::createTorusKnot(dev, pool, 2, 3, 0.40f, 0.088f, 360, 64, "TorusKnot_High");

        // E. Detailed Multi-Part Turbine Rotor with 30 twisted airfoil blades (~38,000 triangles each)
        m_turbineMesh = ProceduralMeshes::createTurbine(dev, pool, 30, 0.20f, 0.68f, 0.35f, 35.0f, 16, 16, "Turbine_High");
    } else {
        // -------------------------------------------------------------
        // HIGH-POLY STRESS-TEST SCENE (>= 1,000,000 visible triangles)
        // -------------------------------------------------------------
        m_groundMesh = Mesh::createGroundGrid(dev, pool, 6.0f, 24);
        m_sphereMesh = Mesh::createSphere(dev, pool, 0.20f, 32, 48);
        m_cubeMesh = Mesh::createCube(dev, pool, 0.45f);
        m_pyramidMesh = Mesh::createPyramid(dev, pool, 0.45f, 0.95f);

        // A. High-resolution Icosphere (subdivisions = 6 -> 81,920 triangles each)
        m_icosphereMesh = ProceduralMeshes::createIcosphere(dev, pool, 0.38f, 6, "Icosphere_Subdiv6");

        // B. Mechanical Gear (40 teeth, 28 radial rings -> ~50,000 triangles each)
        m_gearMesh = ProceduralMeshes::createGear(dev, pool, 40, 0.65f, 0.48f, 0.18f, 0.16f, 28, "Gear_Ultra");

        // C. High-polygon Torus (320 x 128 -> 81,920 triangles each)
        m_torusMesh = ProceduralMeshes::createTorus(dev, pool, 0.50f, 0.13f, 320, 128, "Torus_Ultra");

        // D. Procedural Torus Knot (720 x 128 -> 184,320 triangles each)
        m_torusKnotMesh = ProceduralMeshes::createTorusKnot(dev, pool, 2, 3, 0.40f, 0.088f, 720, 128, "TorusKnot_Ultra");

        // E. Detailed Turbine Rotor (48 blades, 24 span x 30 chord -> ~80,000 triangles each)
        m_turbineMesh = ProceduralMeshes::createTurbine(dev, pool, 48, 0.20f, 0.68f, 0.35f, 35.0f, 24, 30, "Turbine_Ultra");
    }

    // -----------------------------------------------------------------
    // SCENE OBJECT PLACEMENT & PBR MATERIAL ASSIGNMENT
    // -----------------------------------------------------------------
    // 1. Ground platform (textured dielectric, high roughness)
    m_renderObjects.push_back(RenderObject{
        .mesh = m_groundMesh.get(),
        .position = {0.0f, -0.60f, 0.0f},
        .albedo = {0.95f, 0.95f, 0.95f},
        .metallic = 0.0f, .roughness = 0.85f, .ao = 1.0f, .textureStrength = 1.0f, .normalMapStrength = 0.85f
    });

    // 2. Centerpiece Turbine 1: Machined Titanium
    m_renderObjects.push_back(RenderObject{
        .mesh = m_turbineMesh.get(),
        .position = {0.0f, 0.52f, -0.45f},
        .rotationAxis = {0.0f, 0.0f, 1.0f},
        .angularSpeed = 0.45f,
        .albedo = {0.84f, 0.85f, 0.88f},
        .metallic = 1.0f, .roughness = 0.20f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 3. Centerpiece Torus Knot 1: Polished Mirror Chrome
    m_renderObjects.push_back(RenderObject{
        .mesh = m_torusKnotMesh.get(),
        .position = {0.0f, 1.50f, -1.75f},
        .rotationAxis = {0.25f, 1.0f, 0.15f},
        .angularSpeed = 0.35f,
        .albedo = {0.96f, 0.96f, 0.98f},
        .metallic = 1.0f, .roughness = 0.04f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 4. Centerpiece Turbine 2: Cast Industrial Steel (elevated back)
    m_renderObjects.push_back(RenderObject{
        .mesh = m_turbineMesh.get(),
        .position = {0.0f, 1.75f, -3.10f},
        .rotationAxis = {0.12f, 0.0f, 1.0f},
        .angularSpeed = -0.30f,
        .albedo = {0.62f, 0.64f, 0.68f},
        .metallic = 1.0f, .roughness = 0.48f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 5. Left Flank Gear 1: Polished Brass / Gold
    m_renderObjects.push_back(RenderObject{
        .mesh = m_gearMesh.get(),
        .position = {-2.25f, 0.15f, -0.65f},
        .rotationAxis = {0.0f, 1.0f, 0.0f},
        .angularSpeed = -0.40f,
        .albedo = {1.00f, 0.78f, 0.34f},
        .metallic = 1.0f, .roughness = 0.12f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 6. Left Flank Torus 1: Platinum
    m_renderObjects.push_back(RenderObject{
        .mesh = m_torusMesh.get(),
        .position = {-2.35f, 0.35f, 0.85f},
        .rotationAxis = {1.0f, 0.4f, 0.2f},
        .angularSpeed = 0.35f,
        .albedo = {0.90f, 0.89f, 0.88f},
        .metallic = 1.0f, .roughness = 0.08f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 7. Left Flank Icosphere 1: Smooth Emerald Dielectric
    m_renderObjects.push_back(RenderObject{
        .mesh = m_icosphereMesh.get(),
        .position = {-1.35f, -0.08f, 1.55f},
        .rotationAxis = {0.0f, 1.0f, 0.0f},
        .angularSpeed = 0.20f,
        .albedo = {0.06f, 0.75f, 0.35f},
        .metallic = 0.0f, .roughness = 0.05f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 8. Right Flank Torus Knot 2: Brushed Copper
    m_renderObjects.push_back(RenderObject{
        .mesh = m_torusKnotMesh.get(),
        .position = {2.25f, 0.50f, -0.65f},
        .rotationAxis = {-0.25f, 1.0f, 0.20f},
        .angularSpeed = 0.30f,
        .albedo = {0.95f, 0.64f, 0.54f},
        .metallic = 1.0f, .roughness = 0.28f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 9. Right Flank Gear 2: Industrial Dark Gunmetal
    m_renderObjects.push_back(RenderObject{
        .mesh = m_gearMesh.get(),
        .position = {2.35f, 0.15f, 0.85f},
        .rotationAxis = {0.0f, 1.0f, 0.0f},
        .angularSpeed = 0.40f,
        .albedo = {0.38f, 0.40f, 0.44f},
        .metallic = 1.0f, .roughness = 0.40f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 10. Right Flank Icosphere 2: Frosted Ruby Dielectric
    m_renderObjects.push_back(RenderObject{
        .mesh = m_icosphereMesh.get(),
        .position = {1.35f, -0.08f, 1.55f},
        .rotationAxis = {0.0f, 1.0f, 0.0f},
        .angularSpeed = -0.20f,
        .albedo = {0.85f, 0.08f, 0.18f},
        .metallic = 0.0f, .roughness = 0.40f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 11. Right Background Torus 2: Cobalt Blue Metal
    m_renderObjects.push_back(RenderObject{
        .mesh = m_torusMesh.get(),
        .position = {1.35f, 1.45f, -2.60f},
        .rotationAxis = {0.4f, 0.5f, 0.7f},
        .angularSpeed = -0.30f,
        .albedo = {0.25f, 0.50f, 0.92f},
        .metallic = 1.0f, .roughness = 0.60f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 12. Far Background Left: Polished Teal Metal Pyramid
    m_renderObjects.push_back(RenderObject{
        .mesh = m_pyramidMesh.get(),
        .position = {-3.0f, 0.15f, -3.40f},
        .rotationAxis = {0.0f, 1.0f, 0.0f},
        .rotationRadians = 0.35f,
        .angularSpeed = 0.25f,
        .albedo = {0.25f, 0.85f, 0.80f},
        .metallic = 1.0f, .roughness = 0.18f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 13. Far Background Center: Textured Rotating Cube
    m_renderObjects.push_back(RenderObject{
        .mesh = m_cubeMesh.get(),
        .position = {0.0f, 0.15f, -4.10f},
        .rotationAxis = {0.2f, 1.0f, 0.1f},
        .rotationRadians = 0.40f,
        .angularSpeed = 0.35f,
        .albedo = {0.95f, 0.95f, 0.95f},
        .metallic = 0.0f, .roughness = 0.45f, .ao = 1.0f, .textureStrength = 1.0f
    });

    // 14. Far Background Right: Royal Blue Dielectric Pyramid
    m_renderObjects.push_back(RenderObject{
        .mesh = m_pyramidMesh.get(),
        .position = {3.0f, 0.15f, -3.40f},
        .rotationAxis = {0.0f, 1.0f, 0.0f},
        .rotationRadians = -0.50f,
        .angularSpeed = -0.25f,
        .albedo = {0.28f, 0.42f, 0.95f},
        .metallic = 0.0f, .roughness = 0.55f, .ao = 1.0f, .textureStrength = 0.0f
    });

    // 15. Additional Stress-Test Exclusive Showcase Objects
    if (m_stressTestMode) {
        // Sapphire Dielectric Icosphere
        m_renderObjects.push_back(RenderObject{
            .mesh = m_icosphereMesh.get(),
            .position = {-3.10f, 0.35f, -1.65f},
            .rotationAxis = {0.0f, 1.0f, 0.0f},
            .angularSpeed = 0.30f,
            .albedo = {0.10f, 0.28f, 0.92f},
            .metallic = 0.0f, .roughness = 0.15f, .ao = 1.0f, .textureStrength = 0.0f
        });

        // Amber Dielectric Icosphere
        m_renderObjects.push_back(RenderObject{
            .mesh = m_icosphereMesh.get(),
            .position = {3.10f, 0.35f, -1.65f},
            .rotationAxis = {0.0f, 1.0f, 0.0f},
            .angularSpeed = -0.30f,
            .albedo = {0.95f, 0.58f, 0.08f},
            .metallic = 0.0f, .roughness = 0.25f, .ao = 1.0f, .textureStrength = 0.0f
        });
    }

    // 16. Foreground Sphere Showcase: 10 spheres demonstrating roughness progression
    //  Front row (z = +2.45): dielectric red plastic (roughness 0.08 -> 0.90)
    //  Back row  (z = +2.00): gold metal (roughness 0.08 -> 0.90)
    constexpr int kColumns = 5;
    for (int row = 0; row < 2; ++row) {
        const bool metal = (row == 1);
        for (int col = 0; col < kColumns; ++col) {
            const float t = static_cast<float>(col) / static_cast<float>(kColumns - 1);
            m_renderObjects.push_back(RenderObject{
                .mesh = m_sphereMesh.get(),
                .position = {-1.0f + 0.50f * static_cast<float>(col), -0.60f + 0.20f, metal ? 2.00f : 2.45f},
                .albedo = metal ? glm::vec3{1.00f, 0.77f, 0.34f} : glm::vec3{0.80f, 0.10f, 0.08f},
                .metallic = metal ? 1.0f : 0.0f,
                .roughness = 0.08f + 0.82f * t,
                .ao = 1.0f,
                .textureStrength = 0.0f
            });
        }
    }

    // Populate batch manager from m_renderObjects for the Normal scene
    for (size_t i = 0; i < m_renderObjects.size(); ++i) {
        const auto& obj = m_renderObjects[i];
        Material mat{
            .albedo = obj.albedo,
            .metallic = obj.metallic,
            .roughness = obj.roughness,
            .ao = obj.ao,
            .textureStrength = obj.textureStrength,
            .emissive = obj.emissive,
            .normalMapStrength = obj.normalMapStrength
        };
        Instance inst{};
        inst.transform = obj.computeModelMatrix();
        inst.customId = static_cast<uint32_t>(i);
        inst.angularSpeed = obj.angularSpeed;
        inst.rotationAxis = obj.rotationAxis;
        inst.rotationRadians = obj.rotationRadians;
        m_batchManager.addInstance(obj.mesh, mat, inst);
    }
    m_batchManager.rebuildGlobalOffsets();

    if (m_instancingMode != InstancingMode::Normal) {
        m_renderObjects.clear();
        m_batchManager.clear();

        // 1. Add Ground Grid
        Material groundMat{.albedo = {0.85f, 0.85f, 0.85f}, .metallic = 0.0f, .roughness = 0.85f, .ao = 1.0f, .textureStrength = 1.0f};
        glm::mat4 groundTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.60f, 0.0f));
        m_batchManager.addInstance(m_groundMesh.get(), groundMat, groundTransform, 0);
        m_renderObjects.push_back(RenderObject{
            .mesh = m_groundMesh.get(),
            .position = {0.0f, -0.60f, 0.0f},
            .albedo = groundMat.albedo,
            .metallic = groundMat.metallic,
            .roughness = groundMat.roughness,
            .ao = groundMat.ao,
            .textureStrength = groundMat.textureStrength
        });

        uint32_t targetCount = 100;
        if (m_instancingMode == InstancingMode::Count1000) targetCount = 1000;
        else if (m_instancingMode == InstancingMode::Count10000) targetCount = 10000;
        else if (m_instancingMode == InstancingMode::Custom) targetCount = (m_customInstanceCount == 0 ? 1 : m_customInstanceCount);

        if (targetCount == 1) {
            // Single instance verification
            Material knotMat{.albedo = {0.96f, 0.96f, 0.98f}, .metallic = 1.0f, .roughness = 0.05f, .ao = 1.0f, .textureStrength = 0.0f};
            glm::mat4 knotTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.80f, 0.0f));
            m_batchManager.addInstance(m_torusKnotMesh.get(), knotMat, knotTransform, 1);
            m_renderObjects.push_back(RenderObject{
                .mesh = m_torusKnotMesh.get(),
                .position = {0.0f, 0.80f, 0.0f},
                .albedo = knotMat.albedo,
                .metallic = knotMat.metallic,
                .roughness = knotMat.roughness,
                .ao = knotMat.ao,
                .textureStrength = knotMat.textureStrength
            });
        } else {
            // 4 high-performance PBR batches
            const Material matTitanium{.albedo = {0.84f, 0.85f, 0.88f}, .metallic = 1.0f, .roughness = 0.20f, .ao = 1.0f, .textureStrength = 0.0f};
            const Material matChrome{.albedo = {0.96f, 0.96f, 0.98f}, .metallic = 1.0f, .roughness = 0.04f, .ao = 1.0f, .textureStrength = 0.0f};
            const Material matBrass{.albedo = {1.00f, 0.78f, 0.34f}, .metallic = 1.0f, .roughness = 0.15f, .ao = 1.0f, .textureStrength = 0.0f};
            const Material matEmerald{.albedo = {0.06f, 0.75f, 0.35f}, .metallic = 0.0f, .roughness = 0.10f, .ao = 1.0f, .textureStrength = 0.0f};

            const Mesh* meshes[4] = {m_turbineMesh.get(), m_torusKnotMesh.get(), m_gearMesh.get(), m_icosphereMesh.get()};
            const Material mats[4] = {matTitanium, matChrome, matBrass, matEmerald};

            const uint32_t perBatch = targetCount / 4;
            for (uint32_t b = 0; b < 4; ++b) {
                const uint32_t countThisBatch = (b == 3) ? (targetCount - 3 * perBatch) : perBatch;
                if (targetCount <= 100) {
                    const float r = 2.5f + 1.6f * static_cast<float>(b);
                    const float y = 0.1f + 0.15f * static_cast<float>(b);
                    m_batchManager.generateInstancedRing(meshes[b], mats[b], countThisBatch, r, y, 0.55f);
                } else {
                    const float extent = (targetCount >= 10000) ? 42.0f : 16.0f;
                    const float y = 0.1f + 0.1f * static_cast<float>(b);
                    m_batchManager.generateInstancedField(meshes[b], mats[b], countThisBatch, extent, y, (targetCount >= 10000) ? 0.35f : 0.50f);
                }
            }

            // Sync with m_renderObjects for CPU mode testing (up to 2,000)
            const uint32_t cpuLimit = std::min(targetCount, 2000u);
            uint32_t added = 0;
            for (const auto& bPtr : m_batchManager.getBatches()) {
                if (bPtr->getMesh() == m_groundMesh.get()) continue;
                for (const auto& inst : bPtr->getInstances()) {
                    if (added++ >= cpuLimit) break;
                    m_renderObjects.push_back(RenderObject{
                        .mesh = bPtr->getMesh(),
                        .position = inst.position,
                        .scale = {1.0f, 1.0f, 1.0f},
                        .albedo = bPtr->getMaterial().albedo,
                        .metallic = bPtr->getMaterial().metallic,
                        .roughness = bPtr->getMaterial().roughness,
                        .ao = bPtr->getMaterial().ao,
                        .textureStrength = bPtr->getMaterial().textureStrength
                    });
                }
            }
        }
        m_batchManager.rebuildGlobalOffsets();
    }

    SIDERA_LOG_INFO(
        "Renderer",
        "Scene initialized ({} total instances, {} triangles across {} batches).",
        getTotalInstanceCount(),
        getTriangleCount(),
        getInstanceBatchCount()
    );

    rebuildGpuScene();
}

void Renderer::destroySceneMeshes() noexcept {
    m_renderObjects.clear();
    m_turbineMesh.reset();
    m_torusKnotMesh.reset();
    m_torusMesh.reset();
    m_gearMesh.reset();
    m_icosphereMesh.reset();
    m_sphereMesh.reset();
    m_pyramidMesh.reset();
    m_cubeMesh.reset();
    m_groundMesh.reset();
}

void Renderer::recordShadowPass(VkCommandBuffer cmd) {
    m_shadowDrawCalls = 0;
    m_shadowApiDrawCalls = 0;
    if (!m_shadowsEnabled || !m_shadowMap || m_cascadeCount == 0) {
        return;
    }
    if (!isGpuDrivenActive() && m_shadowPipeline == VK_NULL_HANDLE) {
        return;
    }
    if (isGpuDrivenActive() && m_shadowIndirectPipeline == VK_NULL_HANDLE) {
        return;
    }

    m_shadowMap->transitionToAttachment(cmd);

    const VkExtent2D shadowExtent = m_shadowMap->extent();

    for (uint32_t cascadeIdx = 0; cascadeIdx < m_cascadeCount; ++cascadeIdx) {
        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = m_shadowMap->cascadeImageView(cascadeIdx);
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {0, 0};
        renderingInfo.renderArea.extent = shadowExtent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 0;
        renderingInfo.pColorAttachments = nullptr;
        renderingInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(cmd, &renderingInfo);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(shadowExtent.width);
        viewport.height = static_cast<float>(shadowExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = shadowExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Slope-scaled depth bias for depth-only shadow rasterization
        const float biasConstant = 1.25f + static_cast<float>(cascadeIdx) * 0.25f;
        const float biasSlope = 1.75f + static_cast<float>(cascadeIdx) * 0.35f;
        vkCmdSetDepthBias(cmd, biasConstant, 0.0f, biasSlope);

        if (isGpuDrivenActive()) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowIndirectPipeline);
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_shadowIndirectLayout,
                0,
                1,
                &m_cameraDescriptorSets[m_currentFrame],
                0,
                nullptr
            );

            vkCmdPushConstants(
                cmd,
                m_shadowIndirectLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(uint32_t),
                &cascadeIdx
            );

            const uint32_t apiCalls = m_gpuScene->recordIndirectDraws(cmd, m_currentFrame, true /* isShadow */);
            m_shadowApiDrawCalls += apiCalls;
            m_shadowDrawCalls += m_gpuScene->getObjectCount();
        } else {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);

            const glm::mat4& lightSpaceMatrix = m_cascadeMatrices[cascadeIdx];

            const Mesh* lastBoundMesh = nullptr;
            for (const auto& obj : m_renderObjects) {
                if (!obj.mesh) {
                    continue;
                }

                const ShadowPushConstants shadowPc{
                    .lightSpaceMatrix = lightSpaceMatrix,
                    .model = obj.computeModelMatrix()
                };

                vkCmdPushConstants(
                    cmd,
                    m_shadowPipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT,
                    0,
                    sizeof(ShadowPushConstants),
                    &shadowPc
                );

                if (obj.mesh != lastBoundMesh) {
                    obj.mesh->bind(cmd);
                    lastBoundMesh = obj.mesh;
                }

                obj.mesh->draw(cmd);
                ++m_shadowDrawCalls;
                ++m_shadowApiDrawCalls;
            }
        }

        vkCmdEndRendering(cmd);
    }

    m_shadowMap->transitionToShaderRead(cmd);
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin recording command buffer");

    // 0. Reset and write start timestamp (timestamp 0)
    if (m_timestampPool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(cmd, m_timestampPool, m_currentFrame * 4, 4);
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, m_timestampPool, m_currentFrame * 4 + 0);
    }

    // 1. GPU-driven draw command generation (compute pass)
    if (isGpuDrivenActive()) {
        m_gpuScene->resetCullingStats(cmd, m_currentFrame);
        m_gpuScene->recordGenerateCommands(
            cmd,
            m_currentFrame,
            m_frustumCullingEnabled,
            m_occlusionCullingEnabled,
            m_canUseHiZ,
            11,
            glm::vec2(1024.0f, 512.0f)
        );
        m_commandsGenerated = m_gpuScene->getCommandsGenerated();
    } else {
        m_commandsGenerated = 0;
    }

    // Timestamp 1: end of command generation
    if (m_timestampPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, m_timestampPool, m_currentFrame * 4 + 1);
    }

    // 2. Dedicated Directional Shadow Map Pass (Depth-Only)
    if (m_shadowsEnabled) {
        recordShadowPass(cmd);
    } else {
        m_shadowDrawCalls = 0;
        m_shadowApiDrawCalls = 0;
    }

    // Timestamp 2: end of shadow pass
    if (m_timestampPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, m_timestampPool, m_currentFrame * 4 + 2);
    }

    // 3. Main Color + Depth HDR PBR Scene Pass
    const VkImage swapchainImage = m_swapchain->getImage(imageIndex);
    const VkExtent2D extent = m_swapchain->getExtent();

    // Transition swapchain color image: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
    transitionImageLayout(
        cmd,
        swapchainImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    );

    // Transition depth image: UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    transitionImageLayout(
        cmd,
        m_depthBuffer->image(),
        VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    );

    // Begin Vulkan 1.3 Dynamic Rendering with both color and depth attachments
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_swapchain->getImageView(imageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{
        m_clearColor.r,
        m_clearColor.g,
        m_clearColor.b,
        m_clearColor.a
    }};

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_depthBuffer->imageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    m_mainApiDrawCalls = 0;
    m_commandsSubmitted = 0;

    for (const auto& vp : m_viewportManager.getViewports()) {
        vkCmdSetViewport(cmd, 0, 1, &vp.vkViewport);
        vkCmdSetScissor(cmd, 0, 1, &vp.vkScissor);

        const glm::vec4 vpParams(
            static_cast<float>(vp.mode),
            static_cast<float>(vp.normalSpace),
            m_camera.getNearPlane(),
            m_camera.getFarPlane()
        );

        if (isGpuDrivenActive()) {
            VkPipeline activePipeline = m_pipelineIndirect->getHandle();
            if (vp.mode == ViewportMode::Wireframe && m_pipelineWireframe) {
                activePipeline = m_pipelineWireframe->getHandle();
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);

            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelineIndirect->getLayout(),
                0,
                1,
                &m_cameraDescriptorSets[m_currentFrame],
                0,
                nullptr
            );

            MeshPushConstants pc{};
            pc.viewportParams = vpParams;
            vkCmdPushConstants(
                cmd,
                m_pipelineIndirect->getLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(MeshPushConstants),
                &pc
            );

            m_mainApiDrawCalls += m_gpuScene->recordIndirectDraws(cmd, m_currentFrame, false /* isShadow */);
            m_commandsSubmitted = m_gpuScene->getObjectCount();
        } else {
            VkPipeline activePipeline = m_pipeline->getHandle();
            if (vp.mode == ViewportMode::Wireframe && m_pipelineWireframe) {
                activePipeline = m_pipelineWireframe->getHandle();
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);

            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipeline->getLayout(),
                0,
                1,
                &m_cameraDescriptorSets[m_currentFrame],
                0,
                nullptr
            );

            const Mesh* lastBoundMesh = nullptr;
            for (const auto& obj : m_renderObjects) {
                if (!obj.mesh) {
                    continue;
                }

                const MeshPushConstants pushData{
                    .model = obj.computeModelMatrix(),
                    .albedoMetallic = glm::vec4(obj.albedo, obj.metallic),
                    .roughnessAoTex = glm::vec4(obj.roughness, obj.ao, obj.textureStrength, obj.normalMapStrength),
                    .viewportParams = vpParams
                };

                vkCmdPushConstants(
                    cmd,
                    m_pipeline->getLayout(),
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(MeshPushConstants),
                    &pushData
                );

                if (obj.mesh != lastBoundMesh) {
                    obj.mesh->bind(cmd);
                    lastBoundMesh = obj.mesh;
                }

                obj.mesh->draw(cmd);
                ++m_mainApiDrawCalls;
            }
        }
    }

    vkCmdEndRendering(cmd);

    // 4. Build Hierarchical Depth (Hi-Z) pyramid from m_depthBuffer
    if (m_hizBuffers[m_currentFrame]) {
        transitionImageLayout(
            cmd,
            m_depthBuffer->image(),
            VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
        );

        m_hizBuffers[m_currentFrame]->build(
            cmd,
            m_depthBuffer->imageView(),
            extent.width,
            extent.height
        );

        m_hizValid[m_currentFrame] = true;
    }

    // Timestamp 3: end of main pass
    if (m_timestampPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, m_timestampPool, m_currentFrame * 4 + 3);
        m_timestampValid[m_currentFrame] = true;
    }

    // Transition swapchain image: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
    transitionImageLayout(
        cmd,
        swapchainImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_2_NONE
    );

    VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end recording command buffer");
}

void Renderer::submitCommandBuffer(VkCommandBuffer cmd,
                                   VkSemaphore waitSemaphore,
                                   VkSemaphore signalSemaphore,
                                   VkFence fence) const {
    VkSemaphoreSubmitInfo waitSemaphoreInfo{};
    waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSemaphoreInfo.semaphore = waitSemaphore;
    waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    waitSemaphoreInfo.value = 0;
    waitSemaphoreInfo.deviceIndex = 0;

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;
    cmdSubmitInfo.deviceMask = 0;

    VkSemaphoreSubmitInfo signalSemaphoreInfo{};
    signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSemaphoreInfo.semaphore = signalSemaphore;
    signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    signalSemaphoreInfo.value = 0;
    signalSemaphoreInfo.deviceIndex = 0;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

    VK_CHECK(
        vkQueueSubmit2(m_context.getDevice().getGraphicsQueue(), 1, &submitInfo, fence),
        "Failed to submit command buffer to graphics queue"
    );
}

void Renderer::recreateSwapchain(Window& window) {
    if (window.isMinimized() || window.shouldClose()) {
        return;
    }

    const auto fbSize = window.getFramebufferSize();
    if (fbSize.x == 0 || fbSize.y == 0) {
        return;
    }

    m_swapchain->recreate(fbSize.x, fbSize.y);
    const VkExtent2D newExtent = m_swapchain->getExtent();

    m_depthBuffer->recreate(newExtent.width, newExtent.height);
    m_hizValid.fill(false);
    m_frameSync->resizeRenderFinishedSemaphores(m_swapchain->getImageCount());

    m_camera.setViewportSize(newExtent.width, newExtent.height);
    m_viewportManager.updateExtents(newExtent.width, newExtent.height);
    SIDERA_LOG_INFO(
        "Camera",
        "Updated camera aspect ratio to {:.3f} ({}x{} px).",
        m_camera.getAspectRatio(),
        newExtent.width,
        newExtent.height
    );

    if (m_pipeline->getColorFormat() != m_swapchain->getImageFormat() ||
        m_pipeline->getDepthFormat() != m_depthBuffer->format()) {
        m_pipeline = std::make_unique<GraphicsPipeline>(
            m_context.getDevice().getLogicalDevice(),
            m_swapchain->getImageFormat(),
            m_depthBuffer->format(),
            m_cameraDescriptorSetLayout,
            kVertShaderPath,
            kFragShaderPath
        );
        if (m_pipelineIndirect) {
            m_pipelineIndirect = std::make_unique<GraphicsPipeline>(
                m_context.getDevice().getLogicalDevice(),
                m_swapchain->getImageFormat(),
                m_depthBuffer->format(),
                m_cameraDescriptorSetLayout,
                kIndirectVertShaderPath,
                kFragShaderPath
            );
        }
    }

    window.resetResizedFlag();
}

void Renderer::cycleViewportMode() {
    m_viewportManager.cycleActiveViewportMode();
}

void Renderer::setViewportMode(ViewportMode mode) {
    m_viewportManager.setActiveViewportMode(mode);
}

void Renderer::toggleSplitView() {
    m_viewportManager.toggleSplit();
}

void Renderer::toggleNormalSpace() {
    m_viewportManager.toggleNormalSpace();
}

} // namespace sidera
