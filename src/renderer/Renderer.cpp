#include "renderer/Renderer.h"
#include "core/Log.h"
#include "core/Window.h"
#include "renderer/Vertex.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanContext.h"
#include "renderer/vulkan/VulkanDevice.h"

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

void transitionImageLayout(VkCommandBuffer cmd,
                           VkImage image,
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
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
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

Renderer::Renderer(const VulkanContext& context, const Window& window, const glm::vec4& clearColor)
    : m_context(context)
    , m_clearColor(clearColor) {
    const auto fbSize = window.getFramebufferSize();

    m_swapchain = std::make_unique<VulkanSwapchain>(
        m_context.getDevice(),
        m_context.getSurface(),
        fbSize.x,
        fbSize.y,
        true
    );

    const VkExtent2D swapExtent = m_swapchain->getExtent();
    m_camera.setPosition({2.40f, 1.85f, 3.10f});
    m_camera.setOrientation(-128.0f, -24.0f);
    m_camera.setViewportSize(swapExtent.width, swapExtent.height);

    m_frameSync = std::make_unique<VulkanFrameSync>(
        m_context.getDevice().getLogicalDevice(),
        kMaxFramesInFlight,
        m_swapchain->getImageCount()
    );

    try {
        createCameraDescriptorResources();

        m_pipeline = std::make_unique<GraphicsPipeline>(
            m_context.getDevice().getLogicalDevice(),
            m_swapchain->getImageFormat(),
            m_cameraDescriptorSetLayout,
            kVertShaderPath,
            kFragShaderPath
        );

        createCommandPoolsAndBuffers();
        createSceneMeshes();
    } catch (...) {
        destroySceneMeshes();
        destroyCommandPools();
        m_pipeline.reset();
        destroyCameraDescriptorResources();
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
        "Renderer initialized ({} 3D scene objects, ClearColor: [{:.2f}, {:.2f}, {:.2f}, {:.2f}], FramesInFlight: {}).",
        m_renderObjects.size(),
        m_clearColor.r,
        m_clearColor.g,
        m_clearColor.b,
        m_clearColor.a,
        kMaxFramesInFlight
    );
}

Renderer::~Renderer() {
    SIDERA_LOG_INFO("Renderer", "Shutting down Renderer (total rendered frames: {})...", m_renderedFrames);

    m_context.waitIdle();

    destroyCommandPools();
    destroySceneMeshes();
    m_pipeline.reset();
    destroyCameraDescriptorResources();
    m_frameSync.reset();
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
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    VK_CHECK(
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_cameraDescriptorSetLayout),
        "Failed to create camera descriptor set layout"
    );

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = kMaxFramesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
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

    const VkDeviceSize uboSize = sizeof(CameraUniformData);
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

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_cameraDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }

    SIDERA_LOG_INFO(
        "Renderer",
        "Created camera uniform buffers ({} x {} bytes) and descriptor sets.",
        kMaxFramesInFlight,
        uboSize
    );
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

void Renderer::updateCameraUniformBuffer(uint32_t frameIndex) {
    const CameraUniformData ubo = m_camera.getUniformData();
    m_cameraUniformBuffers[frameIndex]->uploadData(&ubo, sizeof(CameraUniformData), 0);
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

void Renderer::createSceneMeshes() {
    m_groundMesh = Mesh::createGroundGrid(m_context.getDevice(), m_commandPools[0], 2.4f, 6);
    m_cubeMesh = Mesh::createCube(m_context.getDevice(), m_commandPools[0], 0.45f);
    m_pyramidMesh = Mesh::createPyramid(m_context.getDevice(), m_commandPools[0], 0.45f, 0.95f);

    m_renderObjects.clear();

    // 1. Checkerboard 3D Ground Grid at Y = -0.50
    m_renderObjects.push_back(RenderObject{
        .mesh = m_groundMesh.get(),
        .position = {0.0f, -0.50f, 0.0f},
        .scale = {1.0f, 1.0f, 1.0f},
        .rotationAxis = {0.0f, 1.0f, 0.0f},
        .rotationRadians = 0.0f,
        .angularSpeed = 0.0f
    });

    // 2. Back-left 3D Pyramid
    m_renderObjects.push_back(RenderObject{
        .mesh = m_pyramidMesh.get(),
        .position = {-1.15f, -0.02f, -0.85f},
        .scale = {1.0f, 1.0f, 1.0f},
        .rotationAxis = {0.0f, 1.0f, 0.0f},
        .rotationRadians = 0.35f,
        .angularSpeed = 0.45f
    });

    // 3. Central Rotating 3D Cube
    m_renderObjects.push_back(RenderObject{
        .mesh = m_cubeMesh.get(),
        .position = {0.0f, 0.05f, 0.0f},
        .scale = {1.0f, 1.0f, 1.0f},
        .rotationAxis = {0.15f, 1.0f, 0.10f},
        .rotationRadians = -0.15f,
        .angularSpeed = 0.35f
    });

    // 4. Front-right 3D Pyramid
    m_renderObjects.push_back(RenderObject{
        .mesh = m_pyramidMesh.get(),
        .position = {1.10f, -0.02f, 0.70f},
        .scale = {0.85f, 0.85f, 0.85f},
        .rotationAxis = {0.0f, 1.0f, 0.0f},
        .rotationRadians = -0.50f,
        .angularSpeed = -0.60f
    });
}

void Renderer::destroySceneMeshes() noexcept {
    m_renderObjects.clear();
    m_pyramidMesh.reset();
    m_cubeMesh.reset();
    m_groundMesh.reset();
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) const {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin recording command buffer");

    const VkImage swapchainImage = m_swapchain->getImage(imageIndex);
    const VkExtent2D extent = m_swapchain->getExtent();

    // Transition swapchain image: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
    transitionImageLayout(
        cmd,
        swapchainImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
    );

    // Begin Vulkan 1.3 Dynamic Rendering
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

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

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
            .model = obj.computeModelMatrix()
        };

        vkCmdPushConstants(
            cmd,
            m_pipeline->getLayout(),
            VK_SHADER_STAGE_VERTEX_BIT,
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
    m_frameSync->resizeRenderFinishedSemaphores(m_swapchain->getImageCount());

    const VkExtent2D newExtent = m_swapchain->getExtent();
    m_camera.setViewportSize(newExtent.width, newExtent.height);
    SIDERA_LOG_INFO(
        "Camera",
        "Updated camera aspect ratio to {:.3f} ({}x{} px).",
        m_camera.getAspectRatio(),
        newExtent.width,
        newExtent.height
    );

    if (m_pipeline->getColorFormat() != m_swapchain->getImageFormat()) {
        m_pipeline = std::make_unique<GraphicsPipeline>(
            m_context.getDevice().getLogicalDevice(),
            m_swapchain->getImageFormat(),
            m_cameraDescriptorSetLayout,
            kVertShaderPath,
            kFragShaderPath
        );
    }

    window.resetResizedFlag();
}

} // namespace sidera
