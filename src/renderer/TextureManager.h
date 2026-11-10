#pragma once

#include "renderer/vulkan/SamplerManager.h"
#include "renderer/vulkan/VulkanTexture.h"
#include "renderer/vulkan/VulkanTextureArray.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sidera {

class VulkanDevice;

struct TextureStats {
    uint32_t totalTextures = 0;
    uint32_t loadedTextures = 0;
    uint32_t cacheHits = 0;
    uint32_t totalMipLevels = 0;
    uint64_t gpuMemoryUsageBytes = 0;
    uint32_t uploadCount = 0;
    uint64_t uploadSizeBytes = 0;
    uint32_t activeSamplerCount = 0;
    uint32_t activeDescriptorCount = 0;
};

class TextureManager {
public:
    TextureManager(const VulkanDevice& device, VkCommandPool transferCommandPool);
    ~TextureManager();

    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;
    TextureManager(TextureManager&&) = delete;
    TextureManager& operator=(TextureManager&&) = delete;

    // Load texture from disk (PNG, JPG, BMP, TGA, HDR, etc.) with automatic caching and deduplication
    std::shared_ptr<VulkanTexture> loadTexture(const std::string& filepath,
                                              bool sRGB = true,
                                              bool generateMips = true);

    // Create texture from raw pixel buffer with automatic caching
    std::shared_ptr<VulkanTexture> createTexture(const std::string& name,
                                                const void* pixels,
                                                uint32_t width,
                                                uint32_t height,
                                                VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                                                bool generateMips = true);

    // Create 2D texture array from layer pixel buffers
    std::shared_ptr<VulkanTextureArray> createTextureArray(const std::string& name,
                                                          const std::vector<const void*>& layerPixels,
                                                          uint32_t width,
                                                          uint32_t height,
                                                          VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
                                                          bool generateMips = true);

    // Create compressed texture (BC1, BC3, BC5, BC7)
    std::shared_ptr<VulkanTexture> createCompressedTexture(const std::string& name,
                                                          const void* data,
                                                          size_t dataSize,
                                                          uint32_t width,
                                                          uint32_t height,
                                                          VkFormat format);

    // Resource lookup
    [[nodiscard]] std::shared_ptr<VulkanTexture> getTexture(const std::string& nameOrPath) const;
    [[nodiscard]] bool hasTexture(const std::string& nameOrPath) const;

    // Release / Remove from cache
    bool releaseTexture(const std::string& nameOrPath);
    void clearCache();

    // Built-in procedural fallbacks (guaranteed valid at all times)
    [[nodiscard]] std::shared_ptr<VulkanTexture> getDefaultWhite() const noexcept { return m_defaultWhite; }
    [[nodiscard]] std::shared_ptr<VulkanTexture> getDefaultBlack() const noexcept { return m_defaultBlack; }
    [[nodiscard]] std::shared_ptr<VulkanTexture> getDefaultFlatNormal() const noexcept { return m_defaultFlatNormal; }
    [[nodiscard]] std::shared_ptr<VulkanTexture> getDefaultMetalRough() const noexcept { return m_defaultMetalRough; }
    [[nodiscard]] std::shared_ptr<VulkanTexture> getDefaultWhiteAo() const noexcept { return m_defaultWhiteAo; }
    [[nodiscard]] std::shared_ptr<VulkanTexture> getProceduralChecker() const noexcept { return m_proceduralChecker; }
    [[nodiscard]] std::shared_ptr<VulkanTexture> getProceduralUvGrid() const noexcept { return m_proceduralUvGrid; }
    [[nodiscard]] std::shared_ptr<VulkanTexture> getProceduralBrickNormal() const noexcept { return m_proceduralBrickNormal; }
    [[nodiscard]] std::shared_ptr<VulkanTexture> getProceduralMetalRough() const noexcept { return m_proceduralMetalRough; }
    [[nodiscard]] std::shared_ptr<VulkanTexture> getProceduralEmissive() const noexcept { return m_proceduralEmissive; }

    [[nodiscard]] SamplerManager& getSamplerManager() noexcept { return m_samplerManager; }
    [[nodiscard]] const SamplerManager& getSamplerManager() const noexcept { return m_samplerManager; }
    [[nodiscard]] TextureStats getStats() const noexcept;

private:
    void initDefaultTextures();
    void recordUpload(VkDeviceSize byteSize, uint32_t mips);

    const VulkanDevice& m_device;
    VkCommandPool m_transferCommandPool;
    SamplerManager m_samplerManager;

    std::unordered_map<std::string, std::shared_ptr<VulkanTexture>> m_textures;
    std::unordered_map<std::string, std::shared_ptr<VulkanTextureArray>> m_textureArrays;

    std::shared_ptr<VulkanTexture> m_defaultWhite;
    std::shared_ptr<VulkanTexture> m_defaultBlack;
    std::shared_ptr<VulkanTexture> m_defaultFlatNormal;
    std::shared_ptr<VulkanTexture> m_defaultMetalRough;
    std::shared_ptr<VulkanTexture> m_defaultWhiteAo;
    std::shared_ptr<VulkanTexture> m_proceduralChecker;
    std::shared_ptr<VulkanTexture> m_proceduralUvGrid;
    std::shared_ptr<VulkanTexture> m_proceduralBrickNormal;
    std::shared_ptr<VulkanTexture> m_proceduralMetalRough;
    std::shared_ptr<VulkanTexture> m_proceduralEmissive;

    mutable uint32_t m_cacheHits = 0;
    uint32_t m_loadedTextures = 0;
    uint32_t m_uploadCount = 0;
    uint64_t m_uploadSizeBytes = 0;
};

} // namespace sidera
