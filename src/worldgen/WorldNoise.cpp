#include "worldgen/WorldNoise.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sidera {

std::atomic<uint64_t> WorldNoise::s_evalCount{0};

namespace {

[[nodiscard]] constexpr float fade(float t) noexcept {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

[[nodiscard]] constexpr float lerp(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

[[nodiscard]] inline glm::vec2 grad2D(int32_t ix, int32_t iz, WorldSeed seed) noexcept {
    const float angle = seed.hash2DToFloat(ix, iz) * (2.0f * std::numbers::pi_v<float>);
    return glm::vec2(std::cos(angle), std::sin(angle));
}

} // namespace

float WorldNoise::value2D(float x, float z, WorldSeed seed) noexcept {
    s_evalCount.fetch_add(1, std::memory_order_relaxed);
    const int32_t ix = static_cast<int32_t>(std::floor(x));
    const int32_t iz = static_cast<int32_t>(std::floor(z));
    const float fx = fade(x - static_cast<float>(ix));
    const float fz = fade(z - static_cast<float>(iz));

    const float v00 = seed.hash2DToFloat(ix,     iz);
    const float v10 = seed.hash2DToFloat(ix + 1, iz);
    const float v01 = seed.hash2DToFloat(ix,     iz + 1);
    const float v11 = seed.hash2DToFloat(ix + 1, iz + 1);

    const float v = lerp(lerp(v00, v10, fx), lerp(v01, v11, fx), fz);
    return v * 2.0f - 1.0f;
}

float WorldNoise::perlin2D(float x, float z, WorldSeed seed) noexcept {
    s_evalCount.fetch_add(1, std::memory_order_relaxed);
    const int32_t ix = static_cast<int32_t>(std::floor(x));
    const int32_t iz = static_cast<int32_t>(std::floor(z));
    const float dx = x - static_cast<float>(ix);
    const float dz = z - static_cast<float>(iz);
    const float u = fade(dx);
    const float v = fade(dz);

    const glm::vec2 g00 = grad2D(ix,     iz,     seed);
    const glm::vec2 g10 = grad2D(ix + 1, iz,     seed);
    const glm::vec2 g01 = grad2D(ix,     iz + 1, seed);
    const glm::vec2 g11 = grad2D(ix + 1, iz + 1, seed);

    const float d00 = g00.x * dx          + g00.y * dz;
    const float d10 = g10.x * (dx - 1.0f) + g10.y * dz;
    const float d01 = g01.x * dx          + g01.y * (dz - 1.0f);
    const float d11 = g11.x * (dx - 1.0f) + g11.y * (dz - 1.0f);

    return std::clamp(lerp(lerp(d00, d10, u), lerp(d01, d11, u), v) * 1.41421356f, -1.0f, 1.0f);
}

float WorldNoise::simplex2D(float x, float z, WorldSeed seed) noexcept {
    s_evalCount.fetch_add(1, std::memory_order_relaxed);
    constexpr float F2 = 0.3660254037844386f; // 0.5 * (sqrt(3) - 1)
    constexpr float G2 = 0.21132486540518713f; // (3 - sqrt(3)) / 6

    const float s = (x + z) * F2;
    const int32_t i = static_cast<int32_t>(std::floor(x + s));
    const int32_t j = static_cast<int32_t>(std::floor(z + s));
    const float t = static_cast<float>(i + j) * G2;

    const float x0 = x - (static_cast<float>(i) - t);
    const float z0 = z - (static_cast<float>(j) - t);

    const int32_t i1 = (x0 > z0) ? 1 : 0;
    const int32_t j1 = (x0 > z0) ? 0 : 1;

    const float x1 = x0 - static_cast<float>(i1) + G2;
    const float z1 = z0 - static_cast<float>(j1) + G2;
    const float x2 = x0 - 1.0f + 2.0f * G2;
    const float z2 = z0 - 1.0f + 2.0f * G2;

    auto contrib = [&](int32_t gi, int32_t gj, float cx, float cz) noexcept -> float {
        float t0 = 0.5f - cx * cx - cz * cz;
        if (t0 <= 0.0f) return 0.0f;
        t0 *= t0;
        const glm::vec2 g = grad2D(gi, gj, seed);
        return t0 * t0 * (g.x * cx + g.y * cz);
    };

    const float n = contrib(i, j, x0, z0) +
                    contrib(i + i1, j + j1, x1, z1) +
                    contrib(i + 1, j + 1, x2, z2);
    return std::clamp(70.0f * n, -1.0f, 1.0f);
}

float WorldNoise::cellular2D(float x, float z, WorldSeed seed) noexcept {
    s_evalCount.fetch_add(1, std::memory_order_relaxed);
    const int32_t ix = static_cast<int32_t>(std::floor(x));
    const int32_t iz = static_cast<int32_t>(std::floor(z));

    const WorldSeed seedX = seed.deriveLayer(11u);
    const WorldSeed seedZ = seed.deriveLayer(29u);

    float minDistSq = 10.0f;
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const int32_t cx = ix + dx;
            const int32_t cz = iz + dz;
            const float px = static_cast<float>(cx) + seedX.hash2DToFloat(cx, cz);
            const float pz = static_cast<float>(cz) + seedZ.hash2DToFloat(cx, cz);
            const float ddx = px - x;
            const float ddz = pz - z;
            minDistSq = std::min(minDistSq, ddx * ddx + ddz * ddz);
        }
    }
    return std::clamp(std::sqrt(minDistSq), 0.0f, 1.0f);
}

float WorldNoise::fbm2D(float x, float z, const NoiseParams& params) noexcept {
    float sum = 0.0f;
    float amp = params.amplitude;
    float freq = params.frequency;
    float norm = 0.0f;

    const float ox = x + params.offset.x;
    const float oz = z + params.offset.y;

    for (uint32_t i = 0; i < params.octaves; ++i) {
        const WorldSeed octSeed = params.seed.deriveLayer(i + 1u);
        sum += perlin2D(ox * freq, oz * freq, octSeed) * amp;
        norm += amp;
        freq *= params.lacunarity;
        amp *= params.persistence;
    }
    return (norm > 1e-6f) ? (sum / norm) : 0.0f;
}

float WorldNoise::ridged2D(float x, float z, const NoiseParams& params, float sharpness) noexcept {
    float sum = 0.0f;
    float amp = params.amplitude;
    float freq = params.frequency;
    float norm = 0.0f;
    float weight = 1.0f;

    const float ox = x + params.offset.x;
    const float oz = z + params.offset.y;

    for (uint32_t i = 0; i < params.octaves; ++i) {
        const WorldSeed octSeed = params.seed.deriveLayer(100u + i);
        float n = 1.0f - std::abs(simplex2D(ox * freq, oz * freq, octSeed));
        n = std::pow(std::clamp(n, 0.0f, 1.0f), sharpness) * weight;
        weight = std::clamp(n * 1.5f, 0.25f, 1.0f);
        sum += n * amp;
        norm += amp;
        freq *= params.lacunarity;
        amp *= params.persistence;
    }
    return (norm > 1e-6f) ? std::clamp(sum / norm, 0.0f, 1.0f) : 0.0f;
}

glm::vec2 WorldNoise::domainWarp2D(float x, float z, float strength, const NoiseParams& warpParams) noexcept {
    NoiseParams pX = warpParams;
    NoiseParams pZ = warpParams;
    pZ.seed = warpParams.seed.deriveLayer(777u);
    pZ.offset += glm::vec2(5.2f, 1.3f);

    const float wx = fbm2D(x, z, pX) * strength;
    const float wz = fbm2D(x, z, pZ) * strength;
    return glm::vec2(x + wx, z + wz);
}

} // namespace sidera
