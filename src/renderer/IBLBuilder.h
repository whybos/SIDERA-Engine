#pragma once

#include "renderer/vulkan/VulkanCubemap.h"
#include "renderer/vulkan/VulkanTexture.h"

#include <filesystem>
#include <memory>

namespace sidera {

class VulkanDevice;

struct IBLMaps {
    std::unique_ptr<VulkanCubemap> envCubemap;
    std::unique_ptr<VulkanCubemap> irradianceMap;
    std::unique_ptr<VulkanCubemap> prefilteredMap;
    std::unique_ptr<VulkanTexture> brdfLut;
};

class IBLBuilder {
public:
    static IBLMaps build(const VulkanDevice& device,
                         VkCommandPool commandPool,
                         const std::filesystem::path& hdrPath = "");

    static std::unique_ptr<VulkanTexture> loadHdrTexture(const VulkanDevice& device,
                                                         VkCommandPool commandPool,
                                                         const std::filesystem::path& hdrPath);

private:
    static std::unique_ptr<VulkanTexture> createProceduralHdrTexture(const VulkanDevice& device,
                                                                    VkCommandPool commandPool,
                                                                    uint32_t width = 512,
                                                                    uint32_t height = 256);
};

} // namespace sidera
