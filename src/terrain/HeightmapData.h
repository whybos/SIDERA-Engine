#pragma once

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sidera {

class VulkanTexture;
class TextureManager;

enum class HeightmapFormat {
    Grayscale8,
    Grayscale16,
    Float32,
    Procedural
};

class HeightmapData {
public:
    HeightmapData() = default;
    ~HeightmapData() = default;

    // Load from disk (8-bit, 16-bit, or float/HDR image)
    bool loadFromFile(const std::string& filepath);

    // Load from raw memory buffer
    bool loadFromMemory(const void* data,
                        uint32_t width,
                        uint32_t height,
                        HeightmapFormat format);

    // Procedural generation using multi-octave domain-warped fractal noise
    void generateProcedural(uint32_t resolution = 513,
                            uint32_t seed = 1337,
                            uint32_t octaves = 6,
                            float frequency = 1.0f);

    // Sampling with bilinear interpolation (u, v in [0.0, 1.0])
    [[nodiscard]] float sampleBilinear(float u, float v) const noexcept;

    // Discrete pixel sampling (clamped)
    [[nodiscard]] float sampleDiscrete(uint32_t x, uint32_t z) const noexcept;

    // Continuous surface normal calculation across the heightmap
    [[nodiscard]] glm::vec3 computeNormal(float u,
                                         float v,
                                         float worldWidth,
                                         float worldLength,
                                         float heightScale) const noexcept;

    // Slope calculation (0.0 = completely flat horizontal, 1.0 = vertical cliff)
    [[nodiscard]] float computeSlope(float u,
                                     float v,
                                     float worldWidth,
                                     float worldLength,
                                     float heightScale) const noexcept;

    // Create GPU texture from heightmap data
    [[nodiscard]] std::shared_ptr<VulkanTexture> createGpuTexture(
        TextureManager& textureManager,
        const std::string& name = "terrain_heightmap"
    ) const;

    [[nodiscard]] bool isValid() const noexcept { return !m_heights.empty() && m_width > 0 && m_height > 0; }
    [[nodiscard]] uint32_t getWidth() const noexcept { return m_width; }
    [[nodiscard]] uint32_t getHeight() const noexcept { return m_height; }
    [[nodiscard]] HeightmapFormat getFormat() const noexcept { return m_format; }
    [[nodiscard]] float getMinNormalized() const noexcept { return m_minVal; }
    [[nodiscard]] float getMaxNormalized() const noexcept { return m_maxVal; }
    [[nodiscard]] const std::vector<float>& getRawData() const noexcept { return m_heights; }

private:
    void calculateExtremes() noexcept;

    std::vector<float> m_heights;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    HeightmapFormat m_format = HeightmapFormat::Procedural;
    float m_minVal = 0.0f;
    float m_maxVal = 1.0f;
};

} // namespace sidera
