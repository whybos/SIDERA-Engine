#pragma once

#include "runtime/RuntimeUI.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sidera {

class VulkanContext;

struct UIVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    float useFont = 0.0f;
};

// Real Vulkan 1.3 2D Runtime UI Renderer (zero Dear ImGui dependency).
// Renders UIDrawCommand batches from sidera::runtime::UISystem with alpha blending,
// scissor clipping, and an embedded ASCII bitmap font atlas.
class UIRenderer {
public:
    UIRenderer() = default;
    ~UIRenderer() { shutdown(); }

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    bool init(const VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat);
    void shutdown() noexcept;

    // Records 2D UI draw calls inside an active dynamic rendering pass.
    void render(VkCommandBuffer cmd,
                VkExtent2D extent,
                const std::vector<sidera::runtime::UIDrawCommand>& drawCommands);

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

private:
    bool createFontAtlas(const VulkanContext& ctx);
    bool createPipeline(const VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat);
    bool createVertexBuffer(const VulkanContext& ctx);
    uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeFilter, VkMemoryPropertyFlags props) const;

    void pushQuad(std::vector<UIVertex>& verts,
                  float x, float y, float w, float h,
                  float u0, float v0, float u1, float v1,
                  float r, float g, float b, float a,
                  float useFont) const;
    void pushText(std::vector<UIVertex>& verts,
                  float x, float y, float scale,
                  const std::string& text,
                  float r, float g, float b, float a) const;

    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descSet = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkImage m_fontImage = VK_NULL_HANDLE;
    VkDeviceMemory m_fontMemory = VK_NULL_HANDLE;
    VkImageView m_fontView = VK_NULL_HANDLE;
    VkSampler m_fontSampler = VK_NULL_HANDLE;

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    void* m_mappedVertices = nullptr;
    size_t m_maxVertices = 32768;

    bool m_initialized = false;
};

} // namespace sidera
