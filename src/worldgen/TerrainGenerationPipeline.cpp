#include "worldgen/TerrainGenerationPipeline.h"
#include "worldgen/WorldNoise.h"

#include <glm/geometric.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace sidera {

TerrainGenerationPipeline::TerrainGenerationPipeline(const WorldGenerationConfig& config) noexcept
    : m_config(config)
    , m_biomeGenerator(config) {}

void TerrainGenerationPipeline::setConfig(const WorldGenerationConfig& config) noexcept {
    m_config = config;
    m_biomeGenerator.setConfig(config);
}

float TerrainGenerationPipeline::evaluateContinentalness(float worldX, float worldZ) const noexcept {
    const float invScale = 1.0f / std::max(m_config.worldScale, 0.1f);
    const WorldSeed contSeed = WorldSeed(m_config.seed).deriveLayer(10u);

    NoiseParams warpP{};
    warpP.frequency = m_config.continentalFrequency * 0.7f * invScale;
    warpP.octaves = 2;
    warpP.seed = contSeed.deriveLayer(1u);
    const glm::vec2 warped = WorldNoise::domainWarp2D(worldX, worldZ, m_config.domainWarpStrength * 0.8f, warpP);

    NoiseParams contP{};
    contP.frequency = m_config.continentalFrequency * invScale;
    contP.octaves = 4;
    contP.persistence = 0.5f;
    contP.lacunarity = 2.0f;
    contP.seed = contSeed;

    return std::clamp(WorldNoise::fbm2D(warped.x, warped.y, contP) + m_config.continentalBias, -1.0f, 1.0f);
}

float TerrainGenerationPipeline::evaluateRawElevation(float worldX, float worldZ,
                                                      float& outContinentalness,
                                                      float& outMountainMask,
                                                      float& outValleyMask) const noexcept {
    const float invScale = 1.0f / std::max(m_config.worldScale, 0.1f);
    const WorldSeed baseSeed(m_config.seed);

    outContinentalness = evaluateContinentalness(worldX, worldZ);

    // Smooth land factor: 0 in deep ocean, smoothly rising across coastlines to 1 inland
    const float landFactor = std::clamp((outContinentalness + 0.15f) / 0.55f, 0.0f, 1.0f);
    const float smoothLand = landFactor * landFactor * (3.0f - 2.0f * landFactor);

    // Domain-warped coordinates for organic mountains & valleys
    NoiseParams warpP{};
    warpP.frequency = m_config.mountainFrequency * 0.65f * invScale;
    warpP.octaves = 3;
    warpP.seed = baseSeed.deriveLayer(20u);
    const glm::vec2 wp = WorldNoise::domainWarp2D(worldX, worldZ, m_config.domainWarpStrength, warpP);

    // 1. Ridged multifractal mountains
    NoiseParams mtnP{};
    mtnP.frequency = m_config.mountainFrequency * invScale;
    mtnP.octaves = 5;
    mtnP.persistence = 0.5f;
    mtnP.lacunarity = 2.05f;
    mtnP.seed = baseSeed.deriveLayer(30u);
    const float ridge = WorldNoise::ridged2D(wp.x, wp.y, mtnP, m_config.ridgeSharpness);
    outMountainMask = ridge * smoothLand;

    // 2. Rolling hills & plains
    NoiseParams hillP{};
    hillP.frequency = m_config.mountainFrequency * 1.6f * invScale;
    hillP.octaves = 4;
    hillP.persistence = 0.45f;
    hillP.seed = baseSeed.deriveLayer(40u);
    const float hills = (WorldNoise::fbm2D(worldX, worldZ, hillP) * 0.5f + 0.5f);

    // 3. Valleys / basins
    NoiseParams valP{};
    valP.frequency = m_config.mountainFrequency * 0.9f * invScale;
    valP.octaves = 3;
    valP.seed = baseSeed.deriveLayer(50u);
    const float valRaw = 1.0f - std::abs(WorldNoise::fbm2D(wp.x, wp.y, valP));
    outValleyMask = std::pow(std::clamp(valRaw, 0.0f, 1.0f), 3.0f) * smoothLand;

    // Combine continental base + hills + ridged mountains - carved valleys
    const float oceanFloor = m_config.seaLevel * 0.55f;
    const float landBase   = m_config.seaLevel + 0.05f + hills * 0.18f;
    float elev = oceanFloor + (landBase - oceanFloor) * smoothLand;
    elev += outMountainMask * m_config.mountainAmplitude * (0.35f + 0.65f * smoothLand);
    elev -= outValleyMask * m_config.valleyCarveStrength;

    return std::clamp(elev, 0.0f, 1.0f);
}

float TerrainGenerationPipeline::evaluateRiverMask(float worldX, float worldZ,
                                                   float continentalness, float rawElev) const noexcept {
    if (!m_config.generateRivers || continentalness < -0.05f || rawElev <= m_config.seaLevel) {
        return 0.0f;
    }

    const float invScale = 1.0f / std::max(m_config.worldScale, 0.1f);
    const WorldSeed rivSeed = WorldSeed(m_config.seed).deriveLayer(60u);

    NoiseParams warpP{};
    warpP.frequency = 0.012f * invScale;
    warpP.octaves = 2;
    warpP.seed = rivSeed.deriveLayer(1u);
    const glm::vec2 wp = WorldNoise::domainWarp2D(worldX, worldZ, 16.0f, warpP);

    NoiseParams rivP{};
    rivP.frequency = 0.014f * invScale;
    rivP.octaves = 2;
    rivP.seed = rivSeed;

    // Zero-crossing of smooth FBM forms continuous meandering river channels
    const float absVal = std::abs(WorldNoise::fbm2D(wp.x, wp.y, rivP));
    if (absVal >= m_config.riverThreshold) {
        return 0.0f;
    }
    const float t = 1.0f - (absVal / std::max(m_config.riverThreshold, 1e-4f));
    // Fade rivers out on extreme mountain peaks
    const float altFade = std::clamp((0.78f - rawElev) / 0.20f, 0.0f, 1.0f);
    return t * t * altFade;
}

float TerrainGenerationPipeline::evaluateLakeMask(float continentalness, float rawElev) const noexcept {
    if (!m_config.generateLakes || continentalness < 0.08f) {
        return 0.0f;
    }
    if (rawElev < m_config.lakeLevel && rawElev > m_config.seaLevel * 0.85f) {
        return std::clamp((m_config.lakeLevel - rawElev) / std::max(m_config.lakeLevel - m_config.seaLevel * 0.85f, 0.01f), 0.0f, 1.0f);
    }
    return 0.0f;
}

glm::vec2 TerrainGenerationPipeline::evaluateFlowDirection(float worldX, float worldZ) const noexcept {
    constexpr float eps = 1.0f;
    float c = 0.0f, m = 0.0f, v = 0.0f;
    const float hL = evaluateRawElevation(worldX - eps, worldZ, c, m, v);
    const float hR = evaluateRawElevation(worldX + eps, worldZ, c, m, v);
    const float hD = evaluateRawElevation(worldX, worldZ - eps, c, m, v);
    const float hU = evaluateRawElevation(worldX, worldZ + eps, c, m, v);

    // Downhill gradient vector (-dh/dx, -dh/dz)
    const glm::vec2 grad(-(hR - hL), -(hU - hD));
    const float len = glm::length(grad);
    return (len > 1e-6f) ? (grad / len) : glm::vec2(0.0f, 0.0f);
}

WorldPointSample TerrainGenerationPipeline::evaluatePoint(float worldX, float worldZ) const noexcept {
    WorldPointSample out{};
    float rawElev = evaluateRawElevation(worldX, worldZ, out.continentalness, out.mountainMask, out.valleyMask);

    out.riverMask = evaluateRiverMask(worldX, worldZ, out.continentalness, rawElev);
    out.lakeMask  = evaluateLakeMask(out.continentalness, rawElev);

    // Controlled river carving & consistent lake basin leveling
    if (out.riverMask > 0.0f) {
        rawElev = std::max(m_config.seaLevel * 0.9f, rawElev - out.riverMask * m_config.riverCarveDepth);
    }
    if (out.lakeMask > 0.0f) {
        rawElev = rawElev * (1.0f - out.lakeMask * 0.65f) + (m_config.lakeLevel - 0.02f) * (out.lakeMask * 0.65f);
    }

    out.normalizedHeight = std::clamp(rawElev, 0.0f, 1.0f);
    out.worldHeight = m_config.heightOffset + out.normalizedHeight * m_config.maxElevation;

    // Estimate slope & downhill water flow direction
    constexpr float eps = 1.0f;
    float c = 0.0f, m = 0.0f, v = 0.0f;
    const float hL = evaluateRawElevation(worldX - eps, worldZ, c, m, v);
    const float hR = evaluateRawElevation(worldX + eps, worldZ, c, m, v);
    const float hD = evaluateRawElevation(worldX, worldZ - eps, c, m, v);
    const float hU = evaluateRawElevation(worldX, worldZ + eps, c, m, v);
    const glm::vec2 grad(-(hR - hL), -(hU - hD));
    const float gradLen = glm::length(grad);
    out.flowDirection = (gradLen > 1e-6f) ? (grad / gradLen) : glm::vec2(0.0f);
    const float slope = std::clamp(gradLen * m_config.maxElevation * 0.5f, 0.0f, 1.0f);

    out.biome = m_biomeGenerator.evaluateBiome(
        worldX, worldZ, out.normalizedHeight, slope, out.continentalness, out.riverMask, out.lakeMask
    );
    return out;
}

bool TerrainGenerationPipeline::generateHeightmap(HeightmapData& outHeightmap, float* outTimeMs) const {
    const auto t0 = std::chrono::high_resolution_clock::now();
    const uint32_t res = std::max(m_config.heightmapResolution, 17u);
    std::vector<float> heights(static_cast<size_t>(res) * res, 0.0f);

    const float invRes = 1.0f / static_cast<float>(res - 1u);
    for (uint32_t iz = 0; iz < res; ++iz) {
        const float v = static_cast<float>(iz) * invRes;
        const float worldZ = (v - 0.5f) * m_config.worldLength;
        for (uint32_t ix = 0; ix < res; ++ix) {
            const float u = static_cast<float>(ix) * invRes;
            const float worldX = (u - 0.5f) * m_config.worldWidth;

            float cont = 0.0f, mtn = 0.0f, val = 0.0f;
            float h = evaluateRawElevation(worldX, worldZ, cont, mtn, val);
            const float riv = evaluateRiverMask(worldX, worldZ, cont, h);
            const float lak = evaluateLakeMask(cont, h);
            if (riv > 0.0f) {
                h = std::max(m_config.seaLevel * 0.9f, h - riv * m_config.riverCarveDepth);
            }
            if (lak > 0.0f) {
                h = h * (1.0f - lak * 0.65f) + (m_config.lakeLevel - 0.02f) * (lak * 0.65f);
            }
            heights[static_cast<size_t>(iz) * res + ix] = std::clamp(h, 0.0f, 1.0f);
        }
    }

    const bool ok = outHeightmap.loadFromMemory(heights.data(), res, res, HeightmapFormat::Float32);
    if (outTimeMs) {
        const auto t1 = std::chrono::high_resolution_clock::now();
        *outTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    }
    return ok;
}

} // namespace sidera
