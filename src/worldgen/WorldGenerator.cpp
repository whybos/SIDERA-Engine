#include "worldgen/WorldGenerator.h"
#include "worldgen/WorldNoise.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>

namespace sidera {

WorldGenerator::WorldGenerator(const WorldGenerationConfig& config)
    : m_config(config)
    , m_pipeline(config)
    , m_regionGenerator(config) {
    m_stats.worldSeed = m_config.seed;
    m_stats.worldScale = m_config.worldScale;
}

void WorldGenerator::setConfig(const WorldGenerationConfig& config) {
    m_config = config;
    m_pipeline.setConfig(config);
    m_regionGenerator.setConfig(config);
    clearRegionCache();
    m_worldGenerated = false;
    m_stats.worldSeed = m_config.seed;
    m_stats.worldScale = m_config.worldScale;
}

void WorldGenerator::setWorldSeed(uint64_t seed) {
    m_config.seed = seed;
    setConfig(m_config);
}

void WorldGenerator::setWorldScale(float scale) {
    m_config.worldScale = std::max(scale, 0.1f);
    setConfig(m_config);
}

void WorldGenerator::setPreset(WorldPreset preset) {
    setConfig(WorldGenerationConfig::fromPreset(preset, m_config.seed));
}

bool WorldGenerator::generateWorld() {
    const auto t0 = std::chrono::high_resolution_clock::now();
    WorldNoise::resetEvaluationCount();
    clearRegionCache();
    m_stats.reset();
    m_stats.worldSeed = m_config.seed;
    m_stats.worldScale = m_config.worldScale;

    float hmMs = 0.0f;
    const bool ok = m_pipeline.generateHeightmap(m_globalHeightmap, &hmMs);
    if (!ok) {
        ++m_stats.failedGenerationCount;
        return false;
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    m_stats.heightmapGenerationTimeMs = hmMs;
    m_stats.totalGenerationTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    m_stats.noiseEvaluationCount = WorldNoise::getEvaluationCount();
    m_stats.heightmapMemoryBytes = static_cast<uint64_t>(m_globalHeightmap.getWidth()) *
                                   m_globalHeightmap.getHeight() * sizeof(float);
    m_worldGenerated = true;

    SIDERA_LOG_INFO(
        "WorldGen",
        "Generated procedural world (seed={}, preset='{}', scale={:.2f}, heightmap={}x{}) in {:.2f} ms.",
        m_config.seed,
        getWorldPresetName(m_config.preset),
        m_config.worldScale,
        m_globalHeightmap.getWidth(),
        m_globalHeightmap.getHeight(),
        m_stats.totalGenerationTimeMs
    );
    return true;
}

const GeneratedRegionData& WorldGenerator::generateRegion(int32_t regionX, int32_t regionZ) {
    const uint64_t key = packRegionKey(regionX, regionZ);
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_regionCache.find(key);
        if (it != m_regionCache.end()) {
            return *it->second;
        }
    }

    auto region = std::make_unique<GeneratedRegionData>(m_regionGenerator.generateRegion(regionX, regionZ));
    const uint64_t hmBytes = region->getHeightmapMemoryBytes();
    const uint64_t bmBytes = region->getBiomeMemoryBytes();
    const float genMs = region->generationTimeMs;
    const uint64_t tris = static_cast<uint64_t>(region->resolution - 1u) * (region->resolution - 1u) * 2ULL;

    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto [it, inserted] = m_regionCache.emplace(key, std::move(region));
    if (inserted) {
        ++m_stats.generatedRegionCount;
        ++m_stats.completedGenerationCount;
        m_stats.biomeGenerationTimeMs += genMs;
        m_stats.heightmapMemoryBytes += hmBytes;
        m_stats.biomeDataMemoryBytes += bmBytes;
        m_stats.generatedTerrainTriangles += tris;
        m_stats.noiseEvaluationCount = WorldNoise::getEvaluationCount();
    }
    return *it->second;
}

const GeneratedRegionData& WorldGenerator::regenerateRegion(int32_t regionX, int32_t regionZ) {
    const uint64_t key = packRegionKey(regionX, regionZ);
    auto region = std::make_unique<GeneratedRegionData>(m_regionGenerator.generateRegion(regionX, regionZ));

    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_regionCache[key] = std::move(region);
    return *m_regionCache[key];
}

bool WorldGenerator::hasRegion(int32_t regionX, int32_t regionZ) const {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return m_regionCache.find(packRegionKey(regionX, regionZ)) != m_regionCache.end();
}

void WorldGenerator::clearRegionCache() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_regionCache.clear();
}

WorldPointSample WorldGenerator::samplePoint(float worldX, float worldZ) const noexcept {
    return m_pipeline.evaluatePoint(worldX, worldZ);
}

BiomeType WorldGenerator::getBiomeAt(float worldX, float worldZ) const noexcept {
    return m_pipeline.evaluatePoint(worldX, worldZ).biome.primaryBiome;
}

float WorldGenerator::getTemperatureAt(float worldX, float worldZ) const noexcept {
    return m_pipeline.evaluatePoint(worldX, worldZ).biome.temperature;
}

float WorldGenerator::getMoistureAt(float worldX, float worldZ) const noexcept {
    return m_pipeline.evaluatePoint(worldX, worldZ).biome.moisture;
}

float WorldGenerator::getHeightAt(float worldX, float worldZ) const noexcept {
    return m_pipeline.evaluatePoint(worldX, worldZ).worldHeight;
}

float WorldGenerator::getContinentalnessAt(float worldX, float worldZ) const noexcept {
    return m_pipeline.evaluatePoint(worldX, worldZ).continentalness;
}

float WorldGenerator::getRiverMaskAt(float worldX, float worldZ) const noexcept {
    return m_pipeline.evaluatePoint(worldX, worldZ).riverMask;
}

float WorldGenerator::getLakeMaskAt(float worldX, float worldZ) const noexcept {
    return m_pipeline.evaluatePoint(worldX, worldZ).lakeMask;
}

} // namespace sidera
