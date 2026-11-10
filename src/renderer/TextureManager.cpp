#include "renderer/TextureManager.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <stb_image.h>

#include <cmath>
#include <filesystem>
#include <vector>

namespace sidera {

TextureManager::TextureManager(const VulkanDevice& device, VkCommandPool transferCommandPool)
    : m_device(device)
    , m_transferCommandPool(transferCommandPool)
    , m_samplerManager(device) {
    SIDERA_LOG_INFO("TextureManager", "Initializing central TextureManager...");
    initDefaultTextures();
    SIDERA_LOG_INFO("TextureManager", "Central TextureManager initialized successfully with {} default textures.",
                    m_textures.size());
}

TextureManager::~TextureManager() {
    clearCache();
}

void TextureManager::clearCache() {
    m_defaultWhite.reset();
    m_defaultBlack.reset();
    m_defaultFlatNormal.reset();
    m_defaultMetalRough.reset();
    m_defaultWhiteAo.reset();
    m_proceduralChecker.reset();
    m_proceduralUvGrid.reset();
    m_proceduralBrickNormal.reset();
    m_proceduralMetalRough.reset();
    m_proceduralEmissive.reset();

    m_textures.clear();
    m_textureArrays.clear();
    m_samplerManager.clear();
}

void TextureManager::initDefaultTextures() {
    // 1. Default White 1x1
    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    m_defaultWhite = createTexture("default_white", whitePixel, 1, 1, VK_FORMAT_R8G8B8A8_SRGB, false);

    // 2. Default Black 1x1
    const uint8_t blackPixel[4] = {0, 0, 0, 255};
    m_defaultBlack = createTexture("default_black", blackPixel, 1, 1, VK_FORMAT_R8G8B8A8_SRGB, false);

    // 3. Default Flat Normal 1x1: (0, 0, 1) in tangent space -> RGB [128, 128, 255]
    const uint8_t flatNormalPixel[4] = {128, 128, 255, 255};
    m_defaultFlatNormal = createTexture("default_flat_normal", flatNormalPixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false);

    // 4. Default Metallic-Roughness 1x1: Roughness = 0.5 (G=128), Metallic = 0.0 (B=0)
    const uint8_t metalRoughPixel[4] = {0, 128, 0, 255};
    m_defaultMetalRough = createTexture("default_metal_rough", metalRoughPixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false);

    // 5. Default White AO 1x1 (AO = 1.0 -> R=255)
    const uint8_t whiteAoPixel[4] = {255, 255, 255, 255};
    m_defaultWhiteAo = createTexture("default_white_ao", whiteAoPixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, false);

    // 6. Procedural Checkerboard 256x256 with 9 mip levels
    {
        constexpr uint32_t w = 256;
        constexpr uint32_t h = 256;
        std::vector<uint8_t> pixels(w * h * 4);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const bool check = ((x / 32) + (y / 32)) % 2 == 0;
                const uint8_t val = check ? 230 : 40;
                const size_t idx = (y * w + x) * 4;
                pixels[idx + 0] = val;
                pixels[idx + 1] = val;
                pixels[idx + 2] = val;
                pixels[idx + 3] = 255;
            }
        }
        m_proceduralChecker = createTexture("procedural_checker", pixels.data(), w, h, VK_FORMAT_R8G8B8A8_SRGB, true);
    }

    // 7. Procedural UV Calibration Grid 256x256 with 9 mip levels
    m_proceduralUvGrid = VulkanTexture::createProceduralTestTexture(m_device, m_transferCommandPool, 256, 256, true);
    m_textures["procedural_uv_grid"] = m_proceduralUvGrid;
    recordUpload(m_proceduralUvGrid->memorySize(), m_proceduralUvGrid->mipLevels());

    // 8. Procedural Brick Tangent-Space Normal Map 256x256 with 9 mip levels
    {
        constexpr uint32_t w = 256;
        constexpr uint32_t h = 256;
        std::vector<uint8_t> normalPixels(w * h * 4);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const uint32_t row = y / 32;
                const uint32_t colShift = (row % 2 == 1) ? 32 : 0;
                const uint32_t shiftedX = (x + colShift) % w;

                const uint32_t inBrickX = shiftedX % 64;
                const uint32_t inBrickY = y % 32;

                const bool isMortar = (inBrickX < 3 || inBrickX >= 61 || inBrickY < 3 || inBrickY >= 29);

                float nx = 0.0f;
                float ny = 0.0f;
                float nz = 1.0f;

                if (isMortar) {
                    nz = 0.5f;
                } else {
                    // Beveled brick edge gradients
                    if (inBrickX < 8) nx = -0.5f * (1.0f - static_cast<float>(inBrickX) / 8.0f);
                    else if (inBrickX >= 56) nx = 0.5f * (static_cast<float>(inBrickX - 56) / 8.0f);

                    if (inBrickY < 8) ny = -0.5f * (1.0f - static_cast<float>(inBrickY) / 8.0f);
                    else if (inBrickY >= 24) ny = 0.5f * (static_cast<float>(inBrickY - 24) / 8.0f);

                    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                    nx /= len;
                    ny /= len;
                    nz /= len;
                }

                const size_t idx = (y * w + x) * 4;
                normalPixels[idx + 0] = static_cast<uint8_t>((nx * 0.5f + 0.5f) * 255.0f);
                normalPixels[idx + 1] = static_cast<uint8_t>((ny * 0.5f + 0.5f) * 255.0f);
                normalPixels[idx + 2] = static_cast<uint8_t>((nz * 0.5f + 0.5f) * 255.0f);
                normalPixels[idx + 3] = 255;
            }
        }
        m_proceduralBrickNormal = createTexture("procedural_brick_normal", normalPixels.data(), w, h,
                                                VK_FORMAT_R8G8B8A8_UNORM, true);
    }

    // 9. Procedural Metallic-Roughness Map 256x256 with 9 mip levels
    // Green = Roughness, Blue = Metallic
    {
        constexpr uint32_t w = 256;
        constexpr uint32_t h = 256;
        std::vector<uint8_t> mrPixels(w * h * 4);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(w);
                const float v = static_cast<float>(y) / static_cast<float>(h);
                const float cx = u * 2.0f - 1.0f;
                const float cy = v * 2.0f - 1.0f;
                const float r = std::sqrt(cx * cx + cy * cy);

                // Alternating metallic and roughness zones
                const uint8_t roughness = static_cast<uint8_t>(std::clamp((0.2f + 0.6f * std::sin(r * 12.0f) * 0.5f + 0.5f) * 255.0f, 10.0f, 255.0f));
                const uint8_t metallic = (r < 0.65f) ? 240 : 20;

                const size_t idx = (y * w + x) * 4;
                mrPixels[idx + 0] = 0;           // Unused / AO
                mrPixels[idx + 1] = roughness;   // Roughness (glTF PBR green)
                mrPixels[idx + 2] = metallic;    // Metallic (glTF PBR blue)
                mrPixels[idx + 3] = 255;
            }
        }
        m_proceduralMetalRough = createTexture("procedural_metallic_roughness", mrPixels.data(), w, h,
                                               VK_FORMAT_R8G8B8A8_UNORM, true);
    }

    // 10. Procedural Emissive Map 256x256 with 9 mip levels
    {
        constexpr uint32_t w = 256;
        constexpr uint32_t h = 256;
        std::vector<uint8_t> emissivePixels(w * h * 4, 0);
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(w);
                const float v = static_cast<float>(y) / static_cast<float>(h);
                const float dx = u - 0.5f;
                const float dy = v - 0.5f;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const bool ring1 = std::abs(dist - 0.35f) < 0.025f;
                const bool ring2 = std::abs(dist - 0.18f) < 0.018f;
                const bool cross = (std::abs(dx) < 0.012f && dist < 0.42f) || (std::abs(dy) < 0.012f && dist < 0.42f);

                uint8_t r = 0, g = 0, b = 0;
                if (ring1) {
                    r = 0; g = 220; b = 255; // Neon cyan ring
                } else if (ring2 || cross) {
                    r = 255; g = 140; b = 20; // Glowing amber core
                }

                const size_t idx = (static_cast<size_t>(y) * w + x) * 4;
                emissivePixels[idx + 0] = r;
                emissivePixels[idx + 1] = g;
                emissivePixels[idx + 2] = b;
                emissivePixels[idx + 3] = 255;
            }
        }
        m_proceduralEmissive = createTexture("procedural_emissive", emissivePixels.data(), w, h,
                                             VK_FORMAT_R8G8B8A8_SRGB, true);
    }
}

std::shared_ptr<VulkanTexture> TextureManager::loadTexture(const std::string& filepath,
                                                          bool sRGB,
                                                          bool generateMips) {
    auto it = m_textures.find(filepath);
    if (it != m_textures.end()) {
        ++m_cacheHits;
        return it->second;
    }

    if (!std::filesystem::exists(filepath)) {
        SIDERA_LOG_WARN("TextureManager", "File not found: '{}'. Returning default white texture.", filepath);
        return m_defaultWhite;
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    const bool isHdr = stbi_is_hdr(filepath.c_str()) != 0;
    std::shared_ptr<VulkanTexture> texture;

    if (isHdr) {
        float* pixels = stbi_loadf(filepath.c_str(), &width, &height, &channels, 4);
        if (!pixels) {
            SIDERA_LOG_ERROR("TextureManager", "Failed to load HDR image '{}'. Reason: {}",
                             filepath, stbi_failure_reason());
            return m_defaultWhite;
        }

        const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
        texture = std::make_shared<VulkanTexture>(
            m_device, m_transferCommandPool, pixels,
            static_cast<uint32_t>(width), static_cast<uint32_t>(height),
            format, 0, filepath, generateMips, VK_NULL_HANDLE
        );
        stbi_image_free(pixels);
    } else {
        stbi_uc* pixels = stbi_load(filepath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            SIDERA_LOG_ERROR("TextureManager", "Failed to load LDR image '{}'. Reason: {}",
                             filepath, stbi_failure_reason());
            return m_defaultWhite;
        }

        const VkFormat format = sRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        texture = std::make_shared<VulkanTexture>(
            m_device, m_transferCommandPool, pixels,
            static_cast<uint32_t>(width), static_cast<uint32_t>(height),
            format, 0, filepath, generateMips, VK_NULL_HANDLE
        );
        stbi_image_free(pixels);
    }

    m_textures[filepath] = texture;
    ++m_loadedTextures;
    recordUpload(texture->memorySize(), texture->mipLevels());
    return texture;
}

std::shared_ptr<VulkanTexture> TextureManager::createTexture(const std::string& name,
                                                            const void* pixels,
                                                            uint32_t width,
                                                            uint32_t height,
                                                            VkFormat format,
                                                            bool generateMips) {
    auto it = m_textures.find(name);
    if (it != m_textures.end()) {
        ++m_cacheHits;
        return it->second;
    }

    auto texture = std::make_shared<VulkanTexture>(
        m_device, m_transferCommandPool, pixels, width, height,
        format, 0, name, generateMips, VK_NULL_HANDLE
    );

    m_textures[name] = texture;
    recordUpload(texture->memorySize(), texture->mipLevels());
    return texture;
}

std::shared_ptr<VulkanTextureArray> TextureManager::createTextureArray(const std::string& name,
                                                                      const std::vector<const void*>& layerPixels,
                                                                      uint32_t width,
                                                                      uint32_t height,
                                                                      VkFormat format,
                                                                      bool generateMips) {
    auto it = m_textureArrays.find(name);
    if (it != m_textureArrays.end()) {
        ++m_cacheHits;
        return it->second;
    }

    auto textureArray = std::make_shared<VulkanTextureArray>(
        m_device, m_transferCommandPool, layerPixels, width, height,
        format, generateMips, VK_NULL_HANDLE, name
    );

    m_textureArrays[name] = textureArray;
    recordUpload(textureArray->memorySize(), textureArray->mipLevels() * textureArray->layerCount());
    return textureArray;
}

std::shared_ptr<VulkanTexture> TextureManager::createCompressedTexture(const std::string& name,
                                                                      const void* data,
                                                                      size_t dataSize,
                                                                      uint32_t width,
                                                                      uint32_t height,
                                                                      VkFormat format) {
    auto it = m_textures.find(name);
    if (it != m_textures.end()) {
        ++m_cacheHits;
        return it->second;
    }

    if (!m_device.isTextureCompressionBCSupported()) {
        SIDERA_LOG_WARN("TextureManager", "BC texture compression unsupported on device. Falling back to default.");
        return m_defaultWhite;
    }

    auto texture = std::make_shared<VulkanTexture>(
        m_device, m_transferCommandPool, data, dataSize, width, height,
        format, name, VK_NULL_HANDLE
    );

    m_textures[name] = texture;
    recordUpload(texture->memorySize(), 1);
    return texture;
}

std::shared_ptr<VulkanTexture> TextureManager::getTexture(const std::string& nameOrPath) const {
    auto it = m_textures.find(nameOrPath);
    if (it != m_textures.end()) {
        ++m_cacheHits;
        return it->second;
    }
    return nullptr;
}

bool TextureManager::hasTexture(const std::string& nameOrPath) const {
    return m_textures.find(nameOrPath) != m_textures.end();
}

bool TextureManager::releaseTexture(const std::string& nameOrPath) {
    auto it = m_textures.find(nameOrPath);
    if (it != m_textures.end()) {
        m_textures.erase(it);
        return true;
    }
    return false;
}

void TextureManager::recordUpload(VkDeviceSize byteSize, uint32_t mips) {
    ++m_uploadCount;
    m_uploadSizeBytes += byteSize;
    (void)mips;
}

TextureStats TextureManager::getStats() const noexcept {
    TextureStats s{};
    s.totalTextures = static_cast<uint32_t>(m_textures.size() + m_textureArrays.size());
    s.loadedTextures = m_loadedTextures;
    s.cacheHits = m_cacheHits;
    s.uploadCount = m_uploadCount;
    s.uploadSizeBytes = m_uploadSizeBytes;
    s.activeSamplerCount = m_samplerManager.getActiveSamplerCount();

    for (const auto& [name, tex] : m_textures) {
        if (tex) {
            s.gpuMemoryUsageBytes += tex->memorySize();
            s.totalMipLevels += tex->mipLevels();
        }
    }
    for (const auto& [name, arr] : m_textureArrays) {
        if (arr) {
            s.gpuMemoryUsageBytes += arr->memorySize();
            s.totalMipLevels += arr->mipLevels() * arr->layerCount();
        }
    }
    return s;
}

} // namespace sidera
