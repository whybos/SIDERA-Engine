#pragma once

#include "renderer/Viewport.h"
#include "renderer/vulkan/GraphicsPipeline.h"

#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <memory>

namespace sidera {

class VulkanDevice;
class Terrain;

class TerrainRenderer {
public:
    TerrainRenderer(const VulkanDevice& device,
                    VkFormat colorFormat,
                    VkFormat depthFormat,
                    VkDescriptorSetLayout cameraDescriptorSetLayout);
    ~TerrainRenderer();

    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;
    TerrainRenderer(TerrainRenderer&&) noexcept = default;
    TerrainRenderer& operator=(TerrainRenderer&&) noexcept = default;

    // Update material descriptor sets with the active terrain textures
    void updateMaterialDescriptors(const Terrain& terrain);

    // Record terrain drawing commands into the command buffer
    void recordDraw(VkCommandBuffer cmd,
                    const Terrain& terrain,
                    VkDescriptorSet cameraDescriptorSet,
                    const Viewport& vp,
                    float nearPlane,
                    float farPlane);

    [[nodiscard]] VkPipelineLayout getPipelineLayout() const noexcept { return m_pipelineLayout; }
    [[nodiscard]] VkPipeline getPipeline() const noexcept { return m_pipeline; }

private:
    void createDescriptorLayout();
    void createDescriptorPool();
    void createPipelines(VkFormat colorFormat, VkFormat depthFormat);
    void cleanup() noexcept;

    const VulkanDevice& m_device;
    VkDescriptorSetLayout m_cameraDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_materialDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_materialDescriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipeline m_pipelineWireframe = VK_NULL_HANDLE;
};

} // namespace sidera
