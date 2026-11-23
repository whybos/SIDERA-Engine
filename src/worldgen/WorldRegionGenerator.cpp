#include "worldgen/WorldRegionGenerator.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace sidera {

WorldRegionGenerator::WorldRegionGenerator(const WorldGenerationConfig& config) noexcept
    : m_config(config)
    , m_pipeline(config) {}

void WorldRegionGenerator::setConfig(const WorldGenerationConfig& config) noexcept {
    m_config = config;
    m_pipeline.setConfig(config);
}

GeneratedRegionData WorldRegionGenerator::generateRegion(int32_t regionX, int32_t regionZ) const {
    const auto t0 = std::chrono::high_resolution_clock::now();

    GeneratedRegionData data{};
    data.regionX = regionX;
    data.regionZ = regionZ;
    data.regionSeed = WorldSeed(m_config.seed).deriveRegion(regionX, regionZ).value();
    data.resolution = std::max(m_config.regionResolution, 9u);
    data.regionSize = m_config.regionSize;

    const size_t count = static_cast<size_t>(data.resolution) * data.resolution;
    data.heights.resize(count);
    data.biomes.resize(count);
    data.temperatures.resize(count);
    data.moistures.resize(count);
    data.continentalness.resize(count);
    data.riverMasks.resize(count);
    data.lakeMasks.resize(count);
    data.flowDirections.resize(count);
    data.splatWeights.resize(count);

    const float originX = static_cast<float>(regionX) * data.regionSize;
    const float originZ = static_cast<float>(regionZ) * data.regionSize;
    const float invRes = 1.0f / static_cast<float>(data.resolution - 1u);

    float minH = std::numeric_limits<float>::max();
    float maxH = -std::numeric_limits<float>::max();

    for (uint32_t iz = 0; iz < data.resolution; ++iz) {
        const float worldZ = originZ + static_cast<float>(iz) * invRes * data.regionSize;
        for (uint32_t ix = 0; ix < data.resolution; ++ix) {
            const float worldX = originX + static_cast<float>(ix) * invRes * data.regionSize;
            const size_t idx = static_cast<size_t>(iz) * data.resolution + ix;

            const WorldPointSample pt = m_pipeline.evaluatePoint(worldX, worldZ);
            data.heights[idx] = pt.normalizedHeight;
            data.biomes[idx] = pt.biome.primaryBiome;
            data.temperatures[idx] = pt.biome.temperature;
            data.moistures[idx] = pt.biome.moisture;
            data.continentalness[idx] = pt.continentalness;
            data.riverMasks[idx] = pt.riverMask;
            data.lakeMasks[idx] = pt.lakeMask;
            data.flowDirections[idx] = pt.flowDirection;
            data.splatWeights[idx] = pt.biome.materialWeights;

            minH = std::min(minH, pt.worldHeight);
            maxH = std::max(maxH, pt.worldHeight);
        }
    }

    data.minHeight = minH;
    data.maxHeight = maxH;
    const auto t1 = std::chrono::high_resolution_clock::now();
    data.generationTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    return data;
}

} // namespace sidera
