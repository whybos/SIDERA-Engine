#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>

namespace sidera {

class GraphicsPipeline {
public:
    GraphicsPipeline(VkDevice device,
                     VkFormat colorAttachmentFormat,
                     VkFormat depthAttachmentFormat,
                     VkDescriptorSetLayout descriptorSetLayout,
                     const std::filesystem::path& vertSpvPath,
                     const std::filesystem::path& fragSpvPath,
                     VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL,
                     VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(GraphicsPipeline&&) = delete;
    GraphicsPipeline& operator=(GraphicsPipeline&&) = delete;

    [[nodiscard]] VkPipeline getHandle() const noexcept { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout getLayout() const noexcept { return m_pipelineLayout; }
    [[nodiscard]] VkFormat getColorFormat() const noexcept { return m_colorFormat; }
    [[nodiscard]] VkFormat getDepthFormat() const noexcept { return m_depthFormat; }
    [[nodiscard]] VkPolygonMode getPolygonMode() const noexcept { return m_polygonMode; }

private:
    void createPipeline(VkDescriptorSetLayout descriptorSetLayout,
                        const std::filesystem::path& vertSpvPath,
                        const std::filesystem::path& fragSpvPath);
    void destroyPipeline() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    VkPolygonMode m_polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags m_cullMode = VK_CULL_MODE_BACK_BIT;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace sidera
