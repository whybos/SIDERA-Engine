#include "terrain/HeightmapData.h"
#include "core/Log.h"
#include "renderer/TextureManager.h"
#include "renderer/vulkan/VulkanTexture.h"

#include <glm/geometric.hpp>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <random>

namespace sidera {

namespace {

// Deterministic 2D gradient noise implementation for procedural terrain generation
class SimplexNoise2D {
public:
    explicit SimplexNoise2D(uint32_t seed) {
        std::vector<uint8_t> p(256);
        std::iota(p.begin(), p.end(), uint8_t{0});
        std::default_random_engine rng(seed);
        std::shuffle(p.begin(), p.end(), rng);

        m_perm.resize(512);
        for (int i = 0; i < 512; ++i) {
            m_perm[i] = p[i & 255];
        }
    }

    [[nodiscard]] float noise(float xin, float yin) const noexcept {
        constexpr float F2 = 0.5f * (1.73205080757f - 1.0f); // (sqrt(3) - 1) / 2
        constexpr float G2 = (3.0f - 1.73205080757f) / 6.0f; // (3 - sqrt(3)) / 6

        const float s = (xin + yin) * F2;
        const int i = fastFloor(xin + s);
        const int j = fastFloor(yin + s);

        const float t = (i + j) * G2;
        const float X0 = i - t;
        const float Y0 = j - t;
        const float x0 = xin - X0;
        const float y0 = yin - Y0;

        int i1, j1;
        if (x0 > y0) {
            i1 = 1; j1 = 0;
        } else {
            i1 = 0; j1 = 1;
        }

        const float x1 = x0 - i1 + G2;
        const float y1 = y0 - j1 + G2;
        const float x2 = x0 - 1.0f + 2.0f * G2;
        const float y2 = y0 - 1.0f + 2.0f * G2;

        const int ii = i & 255;
        const int jj = j & 255;

        const int gi0 = m_perm[ii + m_perm[jj]] % 8;
        const int gi1 = m_perm[ii + i1 + m_perm[jj + j1]] % 8;
        const int gi2 = m_perm[ii + 1 + m_perm[jj + 1]] % 8;

        float n0 = 0.0f, n1 = 0.0f, n2 = 0.0f;

        float t0 = 0.5f - x0 * x0 - y0 * y0;
        if (t0 > 0.0f) {
            t0 *= t0;
            n0 = t0 * t0 * grad(gi0, x0, y0);
        }

        float t1 = 0.5f - x1 * x1 - y1 * y1;
        if (t1 > 0.0f) {
            t1 *= t1;
            n1 = t1 * t1 * grad(gi1, x1, y1);
        }

        float t2 = 0.5f - x2 * x2 - y2 * y2;
        if (t2 > 0.0f) {
            t2 *= t2;
            n2 = t2 * t2 * grad(gi2, x2, y2);
        }

        // Returns roughly in [-1, 1], scale up by 70 to normalize to standard unit amplitude
        return 70.0f * (n0 + n1 + n2);
    }

private:
    [[nodiscard]] static int fastFloor(float x) noexcept {
        const int xi = static_cast<int>(x);
        return x < xi ? xi - 1 : xi;
    }

    [[nodiscard]] static float grad(int hash, float x, float y) noexcept {
        switch (hash & 7) {
            case 0: return  x + y;
            case 1: return -x + y;
            case 2: return  x - y;
            case 3: return -x - y;
            case 4: return  x;
            case 5: return -x;
            case 6: return  y;
            case 7: return -y;
            default: return 0.0f;
        }
    }

    std::vector<uint8_t> m_perm;
};

} // namespace

bool HeightmapData::loadFromFile(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        SIDERA_LOG_ERROR("HeightmapData", "Heightmap file does not exist: '{}'", filepath);
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    // 1. Check for 32-bit floating point / HDR image
    if (stbi_is_hdr(filepath.c_str()) != 0) {
        float* pixels = stbi_loadf(filepath.c_str(), &width, &height, &channels, 1);
        if (!pixels) {
            SIDERA_LOG_ERROR("HeightmapData", "Failed to load float heightmap: '{}'", filepath);
            return false;
        }

        m_width = static_cast<uint32_t>(width);
        m_height = static_cast<uint32_t>(height);
        m_format = HeightmapFormat::Float32;
        m_heights.assign(pixels, pixels + (m_width * m_height));
        stbi_image_free(pixels);

        calculateExtremes();
        SIDERA_LOG_INFO("HeightmapData", "Loaded 32-bit Float heightmap '{}' ({}x{}), range: [{:.3f}, {:.3f}].",
                        filepath, m_width, m_height, m_minVal, m_maxVal);
        return true;
    }

    // 2. Check for 16-bit integer image (e.g. 16-bit grayscale PNG/TIFF)
    if (stbi_is_16_bit(filepath.c_str()) != 0) {
        uint16_t* pixels = stbi_load_16(filepath.c_str(), &width, &height, &channels, 1);
        if (!pixels) {
            SIDERA_LOG_ERROR("HeightmapData", "Failed to load 16-bit heightmap: '{}'", filepath);
            return false;
        }

        m_width = static_cast<uint32_t>(width);
        m_height = static_cast<uint32_t>(height);
        m_format = HeightmapFormat::Grayscale16;
        m_heights.resize(m_width * m_height);

        constexpr float invMax = 1.0f / 65535.0f;
        for (size_t i = 0; i < m_heights.size(); ++i) {
            m_heights[i] = static_cast<float>(pixels[i]) * invMax;
        }
        stbi_image_free(pixels);

        calculateExtremes();
        SIDERA_LOG_INFO("HeightmapData", "Loaded 16-bit Grayscale heightmap '{}' ({}x{}), range: [{:.3f}, {:.3f}].",
                        filepath, m_width, m_height, m_minVal, m_maxVal);
        return true;
    }

    // 3. Fallback to standard 8-bit image
    uint8_t* pixels = stbi_load(filepath.c_str(), &width, &height, &channels, 1);
    if (!pixels) {
        SIDERA_LOG_ERROR("HeightmapData", "Failed to load 8-bit heightmap: '{}' (reason: {})",
                        filepath, stbi_failure_reason());
        return false;
    }

    m_width = static_cast<uint32_t>(width);
    m_height = static_cast<uint32_t>(height);
    m_format = HeightmapFormat::Grayscale8;
    m_heights.resize(m_width * m_height);

    constexpr float invMax = 1.0f / 255.0f;
    for (size_t i = 0; i < m_heights.size(); ++i) {
        m_heights[i] = static_cast<float>(pixels[i]) * invMax;
    }
    stbi_image_free(pixels);

    calculateExtremes();
    SIDERA_LOG_INFO("HeightmapData", "Loaded 8-bit Grayscale heightmap '{}' ({}x{}), range: [{:.3f}, {:.3f}].",
                    filepath, m_width, m_height, m_minVal, m_maxVal);
    return true;
}

bool HeightmapData::loadFromMemory(const void* data,
                                  uint32_t width,
                                  uint32_t height,
                                  HeightmapFormat format) {
    if (!data || width == 0 || height == 0) {
        return false;
    }

    m_width = width;
    m_height = height;
    m_format = format;
    m_heights.resize(m_width * m_height);

    if (format == HeightmapFormat::Float32) {
        const float* src = static_cast<const float*>(data);
        std::copy(src, src + (m_width * m_height), m_heights.begin());
    } else if (format == HeightmapFormat::Grayscale16) {
        const uint16_t* src = static_cast<const uint16_t*>(data);
        constexpr float inv = 1.0f / 65535.0f;
        for (size_t i = 0; i < m_heights.size(); ++i) {
            m_heights[i] = static_cast<float>(src[i]) * inv;
        }
    } else {
        const uint8_t* src = static_cast<const uint8_t*>(data);
        constexpr float inv = 1.0f / 255.0f;
        for (size_t i = 0; i < m_heights.size(); ++i) {
            m_heights[i] = static_cast<float>(src[i]) * inv;
        }
    }

    calculateExtremes();
    return true;
}

void HeightmapData::generateProcedural(uint32_t resolution,
                                      uint32_t seed,
                                      uint32_t octaves,
                                      float frequency) {
    m_width = resolution;
    m_height = resolution;
    m_format = HeightmapFormat::Procedural;
    m_heights.resize(m_width * m_height);

    SimplexNoise2D noiseGen(seed);
    SimplexNoise2D warpGen(seed + 9999);

    const float invRes = 1.0f / static_cast<float>(resolution);

    for (uint32_t y = 0; y < resolution; ++y) {
        const float v = static_cast<float>(y) * invRes;
        for (uint32_t x = 0; x < resolution; ++x) {
            const float u = static_cast<float>(x) * invRes;

            // Coordinate centered in [-0.5, 0.5]
            const float cx = (u - 0.5f) * 2.0f;
            const float cy = (v - 0.5f) * 2.0f;

            // 1. Domain warping for fluid riverbeds and mountain folds
            const float qx = warpGen.noise(cx * 1.5f, cy * 1.5f);
            const float qy = warpGen.noise(cx * 1.5f + 5.2f, cy * 1.5f + 1.3f);

            const float sampleX = (cx + 0.35f * qx) * frequency * 2.5f;
            const float sampleY = (cy + 0.35f * qy) * frequency * 2.5f;

            // 2. Multi-octave Fractal Brownian Motion (FBM)
            float total = 0.0f;
            float amplitude = 1.0f;
            float freq = 1.0f;
            float maxAmp = 0.0f;

            for (uint32_t oct = 0; oct < octaves; ++oct) {
                float n = noiseGen.noise(sampleX * freq, sampleY * freq);

                // Add ridged feature harmonics for rugged crests
                if (oct == 1 || oct == 2) {
                    float ridge = 1.0f - std::abs(n);
                    ridge = ridge * ridge;
                    n = std::lerp(n, ridge, 0.45f);
                }

                total += n * amplitude;
                maxAmp += amplitude;
                amplitude *= 0.5f;
                freq *= 2.05f;
            }

            float h = total / maxAmp; // Normalized to [-1, 1]
            h = h * 0.5f + 0.5f;      // Normalized to [0, 1]

            // 3. Gentle island-style boundary falloff to prevent cliff edges at borders
            const float distFromCenter = std::sqrt(cx * cx + cy * cy);
            const float falloff = std::clamp(1.0f - std::pow(distFromCenter / 1.35f, 3.5f), 0.0f, 1.0f);
            h *= falloff;

            // 4. Non-linear elevation shaping (terracing & low valley basins)
            h = std::pow(h, 1.35f);

            m_heights[static_cast<size_t>(y) * resolution + x] = std::clamp(h, 0.0f, 1.0f);
        }
    }

    calculateExtremes();
    SIDERA_LOG_INFO("HeightmapData", "Procedurally generated heightmap ({}x{}, {} octaves, seed {}), range: [{:.3f}, {:.3f}].",
                    m_width, m_height, octaves, seed, m_minVal, m_maxVal);
}

float HeightmapData::sampleBilinear(float u, float v) const noexcept {
    if (m_heights.empty() || m_width == 0 || m_height == 0) {
        return 0.0f;
    }

    // Clamp coordinates safely
    const float cu = std::clamp(u, 0.0f, 1.0f);
    const float cv = std::clamp(v, 0.0f, 1.0f);

    const float px = cu * static_cast<float>(m_width - 1);
    const float pz = cv * static_cast<float>(m_height - 1);

    const uint32_t x0 = std::min(static_cast<uint32_t>(px), m_width - 1);
    const uint32_t z0 = std::min(static_cast<uint32_t>(pz), m_height - 1);
    const uint32_t x1 = std::min(x0 + 1, m_width - 1);
    const uint32_t z1 = std::min(z0 + 1, m_height - 1);

    const float fx = px - static_cast<float>(x0);
    const float fz = pz - static_cast<float>(z0);

    const float h00 = m_heights[static_cast<size_t>(z0) * m_width + x0];
    const float h10 = m_heights[static_cast<size_t>(z0) * m_width + x1];
    const float h01 = m_heights[static_cast<size_t>(z1) * m_width + x0];
    const float h11 = m_heights[static_cast<size_t>(z1) * m_width + x1];

    const float top = std::lerp(h00, h10, fx);
    const float bot = std::lerp(h01, h11, fx);

    return std::lerp(top, bot, fz);
}

float HeightmapData::sampleDiscrete(uint32_t x, uint32_t z) const noexcept {
    if (m_heights.empty() || m_width == 0 || m_height == 0) {
        return 0.0f;
    }
    const uint32_t cx = std::min(x, m_width - 1);
    const uint32_t cz = std::min(z, m_height - 1);
    return m_heights[static_cast<size_t>(cz) * m_width + cx];
}

glm::vec3 HeightmapData::computeNormal(float u,
                                      float v,
                                      float worldWidth,
                                      float worldLength,
                                      float heightScale) const noexcept {
    if (m_heights.empty() || m_width < 2 || m_height < 2) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }

    const float du = 1.0f / static_cast<float>(m_width - 1);
    const float dv = 1.0f / static_cast<float>(m_height - 1);

    // World-space step distances
    const float dx = 2.0f * du * worldWidth;
    const float dz = 2.0f * dv * worldLength;

    const float hL = sampleBilinear(u - du, v) * heightScale;
    const float hR = sampleBilinear(u + du, v) * heightScale;
    const float hD = sampleBilinear(u, v - dv) * heightScale;
    const float hU = sampleBilinear(u, v + dv) * heightScale;

    // Normal = normalize(-(hR - hL)/dx, 1.0, -(hU - hD)/dz)
    const glm::vec3 normal(-(hR - hL) / dx, 1.0f, -(hU - hD) / dz);
    return glm::normalize(normal);
}

float HeightmapData::computeSlope(float u,
                                  float v,
                                  float worldWidth,
                                  float worldLength,
                                  float heightScale) const noexcept {
    const glm::vec3 n = computeNormal(u, v, worldWidth, worldLength, heightScale);
    return 1.0f - std::clamp(n.y, 0.0f, 1.0f);
}

std::shared_ptr<VulkanTexture> HeightmapData::createGpuTexture(
    TextureManager& textureManager,
    const std::string& name
) const {
    if (!isValid()) {
        return textureManager.getDefaultWhite();
    }

    // Convert float heights to 16-bit UNORM (R16_UNORM) or 8-bit RGBA for maximum GPU compatibility
    std::vector<uint16_t> r16Pixels(m_width * m_height);
    for (size_t i = 0; i < m_heights.size(); ++i) {
        const float clamped = std::clamp(m_heights[i], 0.0f, 1.0f);
        r16Pixels[i] = static_cast<uint16_t>(clamped * 65535.0f);
    }

    return textureManager.createTexture(
        name,
        r16Pixels.data(),
        m_width,
        m_height,
        VK_FORMAT_R16_UNORM,
        false // No mips needed for raw height data
    );
}

void HeightmapData::calculateExtremes() noexcept {
    if (m_heights.empty()) {
        m_minVal = 0.0f;
        m_maxVal = 0.0f;
        return;
    }
    m_minVal = m_heights[0];
    m_maxVal = m_heights[0];
    for (float val : m_heights) {
        m_minVal = std::min(m_minVal, val);
        m_maxVal = std::max(m_maxVal, val);
    }
}

} // namespace sidera
