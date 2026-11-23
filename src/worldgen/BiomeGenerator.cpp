#include "worldgen/BiomeGenerator.h"
#include "worldgen/WorldNoise.h"

#include <algorithm>
#include <cmath>

namespace sidera {

BiomeGenerator::BiomeGenerator(const WorldGenerationConfig& config) noexcept
    : m_config(config) {}

float BiomeGenerator::evaluateTemperature(float worldX, float worldZ, float normalizedAltitude) const noexcept {
    const WorldSeed tempSeed = WorldSeed(m_config.seed).deriveLayer(201u);
    NoiseParams p{};
    p.frequency = 0.006f / std::max(m_config.worldScale, 0.1f);
    p.octaves = 3;
    p.seed = tempSeed;

    const float noise = WorldNoise::fbm2D(worldX, worldZ, p) * 0.35f;
    // Latitude gradient: warmer near Z=0, cooler toward poles
    const float latCooling = std::clamp(std::abs(worldZ) * m_config.latitudeScale, 0.0f, 0.35f);
    // Altitude lapse rate: higher elevations are significantly colder
    const float altCooling = std::max(0.0f, normalizedAltitude - m_config.seaLevel) * 0.85f;

    return std::clamp(0.62f + noise - latCooling - altCooling + m_config.temperatureBias, 0.0f, 1.0f);
}

float BiomeGenerator::evaluateMoisture(float worldX, float worldZ, float continentalness, float riverProximity) const noexcept {
    const WorldSeed moistSeed = WorldSeed(m_config.seed).deriveLayer(202u);
    NoiseParams p{};
    p.frequency = 0.007f / std::max(m_config.worldScale, 0.1f);
    p.octaves = 3;
    p.seed = moistSeed;

    const float noise = WorldNoise::fbm2D(worldX, worldZ, p) * 0.35f;
    // Coastal & river-adjacent regions are wetter; deep inland is drier
    const float coastalBoost = std::clamp((0.25f - continentalness) * 0.35f, -0.15f, 0.25f);
    const float riverBoost = riverProximity * 0.35f;

    return std::clamp(0.48f + noise + coastalBoost + riverBoost + m_config.moistureBias, 0.0f, 1.0f);
}

BiomeSample BiomeGenerator::evaluateBiome(float worldX,
                                          float worldZ,
                                          float normalizedAltitude,
                                          float slope,
                                          float continentalness,
                                          float riverProximity,
                                          float lakeMask) const noexcept {
    BiomeSample s{};
    s.altitude = normalizedAltitude;
    s.slope = slope;
    s.continentalness = continentalness;
    s.riverProximity = riverProximity;
    s.lakeMask = lakeMask;
    s.temperature = evaluateTemperature(worldX, worldZ, normalizedAltitude);
    s.moisture = evaluateMoisture(worldX, worldZ, continentalness, riverProximity);

    // Biome classification based on altitude, temperature, moisture, continentalness, and water proximity
    if (normalizedAltitude <= m_config.seaLevel + 0.045f) {
        s.primaryBiome = BiomeType::Coastal;
        s.secondaryBiome = (s.moisture > 0.65f) ? BiomeType::Wetlands : BiomeType::Grassland;
        s.blendWeight = std::clamp((normalizedAltitude - m_config.seaLevel) / 0.045f, 0.0f, 1.0f);
    } else if (normalizedAltitude >= 0.72f) {
        s.primaryBiome = (s.temperature < 0.38f) ? BiomeType::SnowyMountains : BiomeType::Alpine;
        s.secondaryBiome = (s.primaryBiome == BiomeType::SnowyMountains) ? BiomeType::Alpine : BiomeType::SnowyMountains;
        s.blendWeight = std::clamp((normalizedAltitude - 0.72f) / 0.18f, 0.0f, 1.0f);
    } else if (normalizedAltitude >= 0.56f) {
        s.primaryBiome = BiomeType::Alpine;
        s.secondaryBiome = (s.temperature < 0.35f) ? BiomeType::SnowyMountains : BiomeType::TemperateForest;
        s.blendWeight = std::clamp((normalizedAltitude - 0.56f) / 0.16f, 0.0f, 1.0f);
    } else if (s.temperature < 0.28f) {
        s.primaryBiome = BiomeType::Tundra;
        s.secondaryBiome = BiomeType::Alpine;
        s.blendWeight = std::clamp((0.28f - s.temperature) / 0.28f, 0.0f, 1.0f);
    } else if (s.temperature > 0.68f && s.moisture < 0.32f) {
        s.primaryBiome = BiomeType::Desert;
        s.secondaryBiome = BiomeType::Grassland;
        s.blendWeight = std::clamp((0.32f - s.moisture) / 0.32f, 0.0f, 1.0f);
    } else if (s.temperature > 0.68f && s.moisture >= 0.58f) {
        s.primaryBiome = BiomeType::Tropical;
        s.secondaryBiome = BiomeType::DenseForest;
        s.blendWeight = std::clamp((s.moisture - 0.58f) / 0.42f, 0.0f, 1.0f);
    } else if (s.moisture > 0.74f && (riverProximity > 0.25f || lakeMask > 0.25f || normalizedAltitude < m_config.seaLevel + 0.15f)) {
        s.primaryBiome = BiomeType::Wetlands;
        s.secondaryBiome = BiomeType::TemperateForest;
        s.blendWeight = std::clamp((s.moisture - 0.74f) / 0.26f, 0.0f, 1.0f);
    } else if (s.moisture > 0.62f) {
        s.primaryBiome = BiomeType::DenseForest;
        s.secondaryBiome = BiomeType::TemperateForest;
        s.blendWeight = std::clamp((s.moisture - 0.62f) / 0.38f, 0.0f, 1.0f);
    } else if (s.moisture > 0.40f) {
        s.primaryBiome = BiomeType::TemperateForest;
        s.secondaryBiome = BiomeType::Grassland;
        s.blendWeight = std::clamp((s.moisture - 0.40f) / 0.22f, 0.0f, 1.0f);
    } else {
        s.primaryBiome = BiomeType::Grassland;
        s.secondaryBiome = BiomeType::Desert;
        s.blendWeight = std::clamp((0.40f - s.moisture) / 0.40f, 0.0f, 1.0f);
    }

    computeMaterialWeights(s);
    return s;
}

void BiomeGenerator::computeMaterialWeights(BiomeSample& s) noexcept {
    // Base weights: R=Grass, G=Rock, B=Dirt, A=Sand
    float grass = 0.0f;
    float rock  = 0.0f;
    float dirt  = 0.0f;
    float sand  = 0.0f;
    float snow  = 0.0f;
    float mud   = 0.0f;

    switch (s.primaryBiome) {
        case BiomeType::Grassland:       grass = 0.80f; dirt = 0.15f; rock = 0.05f; break;
        case BiomeType::TemperateForest: grass = 0.65f; dirt = 0.28f; rock = 0.07f; break;
        case BiomeType::DenseForest:     grass = 0.55f; dirt = 0.38f; rock = 0.07f; break;
        case BiomeType::Desert:          sand  = 0.85f; dirt = 0.10f; rock = 0.05f; break;
        case BiomeType::Coastal:         sand  = 0.78f; dirt = 0.12f; grass = 0.10f; break;
        case BiomeType::Alpine:          rock  = 0.65f; dirt = 0.20f; grass = 0.15f; break;
        case BiomeType::SnowyMountains:  rock  = 0.55f; sand = 0.45f; snow = 0.85f; break;
        case BiomeType::Tundra:          dirt  = 0.45f; grass = 0.30f; rock = 0.25f; snow = 0.40f; break;
        case BiomeType::Wetlands:        dirt  = 0.50f; grass = 0.45f; sand = 0.05f; mud = 0.65f; break;
        case BiomeType::Tropical:        grass = 0.75f; dirt = 0.20f; sand = 0.05f; break;
        default:                         grass = 1.0f; break;
    }

    // Steep slopes increase rock exposure
    const float slopeRock = std::clamp((s.slope - 0.30f) * 2.2f, 0.0f, 1.0f);
    rock += slopeRock * 1.4f;
    grass *= (1.0f - slopeRock * 0.85f);

    // Rivers & lakes increase mud / sand shoreline
    if (s.riverProximity > 0.2f || s.lakeMask > 0.2f) {
        const float w = std::max(s.riverProximity, s.lakeMask);
        mud = std::max(mud, w * 0.7f);
        sand += w * 0.45f;
        dirt += w * 0.35f;
    }

    // High altitude snow factor
    if (s.altitude > 0.70f && s.temperature < 0.45f) {
        snow = std::clamp((s.altitude - 0.70f) * 3.5f, snow, 1.0f);
    }

    // Normalize RGBA splat weights strictly to sum = 1.0
    const float sum = grass + rock + dirt + sand;
    if (sum > 1e-5f) {
        const float inv = 1.0f / sum;
        s.materialWeights = glm::vec4(grass * inv, rock * inv, dirt * inv, sand * inv);
    } else {
        s.materialWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    }
    s.snowWeight = snow;
    s.mudWeight = mud;
}

} // namespace sidera
