#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>

namespace sidera {

class GraphicsPipeline {
public:
    GraphicsPipeline(VkDevice device,
                     VkFormat colorAttachmentFormat,
                     VkDescriptorSetLayout descriptorSetLayout,
                     const std::filesystem::path& vertSpvPath,
                     const std::filesystem::path& fragSpvPath);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(GraphicsPipeline&&) = delete;
    GraphicsPipeline& operator=(GraphicsPipeline&&) = delete;

    [[nodiscard]] VkPipeline getHandle() const noexcept { return m_pipeline; }
    [[nodiscard]] VkPipelineLayout getLayout() const noexcept { return m_pipelineLayout; }
    [[nodiscard]] VkFormat getColorFormat() const noexcept { return m_colorFormat; }

private:
    void createPipeline(VkDescriptorSetLayout descriptorSetLayout,
                        const std::filesystem::path& vertSpvPath,
                        const std::filesystem::path& fragSpvPath);
    void destroyPipeline() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace sidera
