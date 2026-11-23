#pragma once

#include "worldgen/WorldSeed.h"

#include <glm/vec2.hpp>
#include <atomic>
#include <cstdint>

namespace sidera {

struct NoiseParams {
    float frequency = 0.02f;
    float amplitude = 1.0f;
    uint32_t octaves = 5;
    float persistence = 0.5f;
    float lacunarity = 2.0f;
    WorldSeed seed{1337ULL};
    glm::vec2 offset{0.0f, 0.0f};
};

// Stateless, deterministic 2D procedural noise library.
class WorldNoise {
public:
    // Value noise in [-1, 1]
    [[nodiscard]] static float value2D(float x, float z, WorldSeed seed) noexcept;

    // Perlin gradient noise in [-1, 1]
    [[nodiscard]] static float perlin2D(float x, float z, WorldSeed seed) noexcept;

    // 2D Simplex noise in [-1, 1]
    [[nodiscard]] static float simplex2D(float x, float z, WorldSeed seed) noexcept;

    // Cellular / Worley F1 distance noise in [0, 1]
    [[nodiscard]] static float cellular2D(float x, float z, WorldSeed seed) noexcept;

    // Fractal Brownian Motion (Perlin-based) normalized to approximately [-1, 1]
    [[nodiscard]] static float fbm2D(float x, float z, const NoiseParams& params) noexcept;

    // Ridged Multifractal Noise in [0, 1] for sharp mountain ridges and crests
    [[nodiscard]] static float ridged2D(float x, float z, const NoiseParams& params, float sharpness = 1.8f) noexcept;

    // Domain Warping: warps input (x, z) using two orthogonal FBM fields
    [[nodiscard]] static glm::vec2 domainWarp2D(float x, float z, float strength, const NoiseParams& warpParams) noexcept;

    // Optional evaluation counter for telemetry
    static void resetEvaluationCount() noexcept { s_evalCount.store(0, std::memory_order_relaxed); }
    [[nodiscard]] static uint64_t getEvaluationCount() noexcept { return s_evalCount.load(std::memory_order_relaxed); }

private:
    static std::atomic<uint64_t> s_evalCount;
};

} // namespace sidera
