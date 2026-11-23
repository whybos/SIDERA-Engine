#pragma once

#include <cstdint>
#include <string_view>

namespace sidera {

enum class WorldPreset : uint8_t {
    Balanced = 0,
    Continental,
    Mountainous,
    Archipelago,
    DesertWorld,
    AlpineWorld
};

[[nodiscard]] constexpr std::string_view getWorldPresetName(WorldPreset preset) noexcept {
    switch (preset) {
        case WorldPreset::Balanced:    return "Balanced";
        case WorldPreset::Continental: return "Continental";
        case WorldPreset::Mountainous: return "Mountainous";
        case WorldPreset::Archipelago: return "Archipelago";
        case WorldPreset::DesertWorld: return "Desert World";
        case WorldPreset::AlpineWorld: return "Alpine World";
    }
    return "Custom";
}

struct WorldGenerationConfig {
    uint64_t seed = 424242ULL;
    WorldPreset preset = WorldPreset::Balanced;

    // World spatial scale (meters across full demo heightmap / continental feature scale)
    float worldScale = 1.0f;
    float worldWidth = 120.0f;
    float worldLength = 120.0f;
    float maxElevation = 18.0f;
    float heightOffset = -2.2f;
    float seaLevel = 0.22f; // normalized [0..1]

    // Continental landmass parameters
    float continentalFrequency = 0.008f;
    float continentalBias = 0.15f;     // positive = more land, negative = more ocean

    // Mountain & valley parameters
    float mountainFrequency = 0.022f;
    float mountainAmplitude = 0.55f;
    float ridgeSharpness = 1.8f;
    float valleyCarveStrength = 0.20f;
    float domainWarpStrength = 12.0f;

    // Hydrology parameters (rivers & lakes)
    bool generateRivers = true;
    bool generateLakes = true;
    float riverThreshold = 0.055f;     // lower = narrower river channels
    float riverCarveDepth = 0.08f;
    float lakeLevel = 0.28f;           // normalized elevation below which inland basins form lakes

    // Climate parameters
    float temperatureBias = 0.0f;      // [-0.5 .. +0.5]
    float moistureBias = 0.0f;         // [-0.5 .. +0.5]
    float latitudeScale = 0.004f;

    // Region generation resolution
    uint32_t heightmapResolution = 513;
    uint32_t regionResolution = 65;
    float regionSize = 30.0f;

    [[nodiscard]] static WorldGenerationConfig fromPreset(WorldPreset preset, uint64_t seed = 424242ULL) noexcept {
        WorldGenerationConfig cfg{};
        cfg.seed = seed;
        cfg.preset = preset;
        switch (preset) {
            case WorldPreset::Balanced:
                cfg.continentalFrequency = 0.008f;
                cfg.continentalBias = 0.18f;
                cfg.mountainFrequency = 0.022f;
                cfg.mountainAmplitude = 0.55f;
                cfg.valleyCarveStrength = 0.20f;
                cfg.temperatureBias = 0.0f;
                cfg.moistureBias = 0.05f;
                break;
            case WorldPreset::Continental:
                cfg.continentalFrequency = 0.005f;
                cfg.continentalBias = 0.28f;
                cfg.mountainFrequency = 0.018f;
                cfg.mountainAmplitude = 0.48f;
                cfg.valleyCarveStrength = 0.18f;
                cfg.temperatureBias = 0.02f;
                cfg.moistureBias = 0.05f;
                break;
            case WorldPreset::Mountainous:
                cfg.continentalFrequency = 0.009f;
                cfg.continentalBias = 0.35f;
                cfg.mountainFrequency = 0.028f;
                cfg.mountainAmplitude = 0.85f;
                cfg.ridgeSharpness = 2.4f;
                cfg.valleyCarveStrength = 0.28f;
                cfg.temperatureBias = -0.10f;
                cfg.moistureBias = 0.0f;
                break;
            case WorldPreset::Archipelago:
                cfg.continentalFrequency = 0.018f;
                cfg.continentalBias = -0.22f;
                cfg.seaLevel = 0.36f;
                cfg.mountainFrequency = 0.026f;
                cfg.mountainAmplitude = 0.42f;
                cfg.temperatureBias = 0.15f;
                cfg.moistureBias = 0.25f;
                break;
            case WorldPreset::DesertWorld:
                cfg.continentalFrequency = 0.007f;
                cfg.continentalBias = 0.35f;
                cfg.seaLevel = 0.12f;
                cfg.mountainFrequency = 0.016f;
                cfg.mountainAmplitude = 0.35f;
                cfg.generateRivers = true;
                cfg.riverThreshold = 0.03f;
                cfg.temperatureBias = 0.38f;
                cfg.moistureBias = -0.38f;
                break;
            case WorldPreset::AlpineWorld:
                cfg.continentalFrequency = 0.008f;
                cfg.continentalBias = 0.30f;
                cfg.mountainFrequency = 0.025f;
                cfg.mountainAmplitude = 0.78f;
                cfg.ridgeSharpness = 2.2f;
                cfg.temperatureBias = -0.32f;
                cfg.moistureBias = 0.18f;
                break;
        }
        return cfg;
    }
};

} // namespace sidera
