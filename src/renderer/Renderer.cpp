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

#include <array>

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

namespace {
constexpr const char* kVertShaderPath = "shaders/triangle.vert.spv";
constexpr const char* kFragShaderPath = "shaders/triangle.frag.spv";
constexpr const char* kShadowVertShaderPath = "shaders/shadow_depth.vert.spv";

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
                   uint32_t shadowDebugMode)
    : m_context(context)
    , m_clearColor(clearColor)
    , m_shadowsEnabled(shadowsEnabled)
    , m_shadowDebugMode(shadowDebugMode) {
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
        shadowResolution
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
        m_albedoTexture = VulkanTexture::createProceduralTestTexture(m_context.getDevice(), m_commandPools[0], 256, 256);
        m_iblMaps = IBLBuilder::build(m_context.getDevice(), m_commandPools[0]);
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
        createSceneMeshes();
    } catch (...) {
        destroySceneMeshes();
        destroyShadowPipeline();
        m_pipeline.reset();
        destroyCameraDescriptorResources();
        m_iblMaps = {};
        m_albedoTexture.reset();
        m_shadowMap.reset();
        destroyCommandPools();
        throw;
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
        "Renderer initialized ({} 3D scene objects, Depth: ENABLED, PBR: ENABLED, Directional Shadows: {} ({}x{} px), FramesInFlight: {}).",
        m_renderObjects.size(),
        m_shadowsEnabled ? "ENABLED" : "DISABLED",
        m_shadowMap->resolution(),
        m_shadowMap->resolution(),
        kMaxFramesInFlight
    );
}

Renderer::~Renderer() {
    SIDERA_LOG_INFO("Renderer", "Shutting down Renderer (total rendered frames: {})...", m_renderedFrames);

    m_context.waitIdle();

    destroySceneMeshes();
    destroyShadowPipeline();
    m_pipeline.reset();
    destroyCameraDescriptorResources();
    m_iblMaps = {};
    m_albedoTexture.reset();
    m_shadowMap.reset();
    destroyCommandPools();
    m_frameSync.reset();
    m_depthBuffer.reset();
    m_swapchain.reset();
}

void Renderer::updateScene(float deltaTime) noexcept {
    for (auto& obj : m_renderObjects) {
        if (obj.angularSpeed != 0.0f) {
            obj.rotationRadians += obj.angularSpeed * deltaTime;
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

    // 4. Update the current frame's camera uniform buffer
    updateCameraUniformBuffer(m_currentFrame);

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

    const std::array<VkDescriptorSetLayoutBinding, 6> bindings = {
        uboLayoutBinding,
        samplerLayoutBinding,
        irradianceLayoutBinding,
        prefilteredLayoutBinding,
        brdfLutLayoutBinding,
        shadowLayoutBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VK_CHECK(
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_cameraDescriptorSetLayout),
        "Failed to create camera descriptor set layout"
    );

    const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFramesInFlight},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFramesInFlight * 5}
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

        std::array<VkWriteDescriptorSet, 6> writes{};
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

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    SIDERA_LOG_INFO(
        "Renderer",
        "Created camera uniform buffers ({} x {} bytes) and descriptor sets with shadow map binding.",
        kMaxFramesInFlight,
        uboSize
    );
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

glm::mat4 Renderer::computeLightSpaceMatrix() const noexcept {
    const glm::vec3 sunDir = glm::normalize(glm::vec3(-0.45f, 0.80f, 0.40f));
    const glm::vec3 sceneCenter(0.0f, 0.5f, -0.5f);
    constexpr float kLightDistance = 20.0f;
    const glm::vec3 lightPos = sceneCenter + sunDir * kLightDistance;
    const glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));

    constexpr float kOrthoHalfSize = 8.5f;
    glm::mat4 lightProj = glm::ortho(
        -kOrthoHalfSize, kOrthoHalfSize,
        -kOrthoHalfSize, kOrthoHalfSize,
        1.0f, 40.0f
    );
    lightProj[1][1] *= -1.0f; // Invert Y for Vulkan clip space
    return lightProj * lightView;
}

void Renderer::setShadowResolution(uint32_t resolution) {
    if (m_shadowMap && m_shadowMap->resolution() == resolution) {
        return;
    }
    m_context.waitIdle();
    m_shadowMap->recreate(resolution);
    updateShadowDescriptorSets();
    SIDERA_LOG_INFO("Renderer", "Shadow map resolution updated to {}x{} px.", resolution, resolution);
}

void Renderer::cycleShadowDebugMode() noexcept {
    m_shadowDebugMode = (m_shadowDebugMode + 1) % 4;
}

void Renderer::createShadowPipeline() {
    const VkDevice device = m_context.getDevice().getLogicalDevice();

    VulkanShader vertShader(device, kShadowVertShaderPath);

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
    pushConstantRange.size = sizeof(ShadowPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(
        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_shadowPipelineLayout),
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
    pipelineInfo.layout = m_shadowPipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;

    VK_CHECK(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowPipeline),
        "Failed to create shadow graphics pipeline"
    );

    SIDERA_LOG_INFO("Pipeline", "Created dedicated depth-only shadow graphics pipeline.");
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

void Renderer::updateCameraUniformBuffer(uint32_t frameIndex) {
    SceneUniformData ubo{};
    ubo.camera = m_camera.getUniformData();
    ubo.lightSpaceMatrix = computeLightSpaceMatrix();
    ubo.cameraPos = glm::vec4(m_camera.getPosition(), 1.0f);

    // Warm key light (directional sun) + one warm and one cool point light
    ubo.sunDirection = glm::vec4(glm::normalize(glm::vec3(-0.45f, 0.80f, 0.40f)), 0.0f);
    ubo.sunColor = glm::vec4(1.0f, 0.95f, 0.85f, 2.6f);
    ubo.pointPosIntensity[0] = glm::vec4(-1.6f, 1.1f, 1.2f, 5.5f);
    ubo.pointColor[0] = glm::vec4(1.0f, 0.65f, 0.35f, 0.0f);
    ubo.pointPosIntensity[1] = glm::vec4(1.9f, 1.0f, -0.6f, 5.5f);
    ubo.pointColor[1] = glm::vec4(0.35f, 0.60f, 1.0f, 0.0f);

    ubo.shadowParams = glm::vec4(
        m_shadowsEnabled ? 1.0f : 0.0f,
        m_shadowDepthBias,
        3.0f,
        static_cast<float>(m_shadowDebugMode)
    );

    const auto extent = m_swapchain->getExtent();
    ubo.screenParams = glm::vec4(
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        0.0f,
        0.0f
    );

    m_cameraUniformBuffers[frameIndex]->uploadData(&ubo, sizeof(SceneUniformData), 0);
}

uint32_t Renderer::getTriangleCount() const noexcept {
    uint32_t triangles = 0;
    for (const auto& obj : m_renderObjects) {
        if (obj.mesh) {
            triangles += obj.mesh->getIndexCount() / 3;
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
        .metallic = 0.0f, .roughness = 0.85f, .ao = 1.0f, .textureStrength = 1.0f
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

    SIDERA_LOG_INFO(
        "Renderer",
        "Scene initialized ({} objects, {} visible triangles across {} draw calls).",
        m_renderObjects.size(),
        getTriangleCount(),
        getDrawCallCount()
    );
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
    if (!m_shadowsEnabled || !m_shadowMap || m_shadowPipeline == VK_NULL_HANDLE) {
        return;
    }

    m_shadowMap->transitionToAttachment(cmd);

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_shadowMap->imageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    const VkExtent2D shadowExtent = m_shadowMap->extent();

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = shadowExtent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.pColorAttachments = nullptr;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);

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

    // Apply slope-scaled depth bias for depth-only shadow rasterization
    vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 1.75f);

    const glm::mat4 lightSpaceMatrix = computeLightSpaceMatrix();

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
    }

    vkCmdEndRendering(cmd);

    m_shadowMap->transitionToShaderRead(cmd);
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin recording command buffer");

    // 1. Dedicated Directional Shadow Map Pass (Depth-Only)
    if (m_shadowsEnabled) {
        recordShadowPass(cmd);
    } else {
        m_shadowDrawCalls = 0;
    }

    // 2. Main Color + Depth HDR PBR Scene Pass
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
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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

    // Bind graphics pipeline and configure dynamic viewport/scissor for current swapchain extent
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getHandle());

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind camera uniform buffer descriptor set (set = 0)
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

    // Draw all 3D mesh instances in the scene using per-object push constants
    const Mesh* lastBoundMesh = nullptr;
    for (const auto& obj : m_renderObjects) {
        if (!obj.mesh) {
            continue;
        }

        const MeshPushConstants pushData{
            .model = obj.computeModelMatrix(),
            .albedoMetallic = glm::vec4(obj.albedo, obj.metallic),
            .roughnessAoTex = glm::vec4(obj.roughness, obj.ao, obj.textureStrength, 0.0f)
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
    }

    vkCmdEndRendering(cmd);

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
    m_frameSync->resizeRenderFinishedSemaphores(m_swapchain->getImageCount());

    m_camera.setViewportSize(newExtent.width, newExtent.height);
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
    }

    window.resetResizedFlag();
}

} // namespace sidera
