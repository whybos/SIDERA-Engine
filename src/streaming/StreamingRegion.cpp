#include "streaming/StreamingRegion.h"
#include "terrain/Terrain.h"
#include "terrain/TerrainConfig.h"
#include "terrain/HeightmapData.h"
#include "foliage/FoliageSystem.h"
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace sidera {

StreamingRegion::StreamingRegion(uint32_t regionId,
                                 uint32_t gridX,
                                 uint32_t gridZ,
                                 const AABB& worldBounds,
                                 RegionBiome biome)
    : m_regionId(regionId)
    , m_gridX(gridX)
    , m_gridZ(gridZ)
    , m_worldBounds(worldBounds)
    , m_biome(biome) {
}

StreamingRegion::~StreamingRegion() {
    unloadCpu();
}

bool StreamingRegion::loadCpu(const TerrainConfig& terrainConfig,
                              const HeightmapData& heightmap,
                              const FoliageSystem* foliageSystem) {
    m_state = StreamingState::Loading;

    // 1. Generate procedural or heightmap-based TerrainChunk (CPU only)
    m_terrainChunk = std::make_unique<TerrainChunk>(
        m_regionId,
        m_gridX,
        m_gridZ,
        terrainConfig,
        heightmap
    );

    // 2. Generate procedural foliage instances appropriate for this region's biome
    generateFoliage(heightmap, terrainConfig, foliageSystem);

    // 3. Compute accurate CPU memory footprint
    m_cpuMemoryBytes = sizeof(StreamingRegion);
    if (m_terrainChunk) {
        m_cpuMemoryBytes += m_terrainChunk->getCpuMemoryUsageBytes();
    }
    m_cpuMemoryBytes += m_foliageInstances.size() * sizeof(FoliageInstance);
    m_cpuMemoryBytes += m_foliageInstanceIds.size() * sizeof(uint32_t);

    m_state = StreamingState::CPUReady;
    return true;
}

bool StreamingRegion::uploadGpu(const VulkanDevice& device, VkCommandPool transferCommandPool) {
    if (m_state != StreamingState::UploadQueued && m_state != StreamingState::CPUReady) {
        return false;
    }

    TerrainChunk* chunk = getTerrainChunk();
    if (!chunk) {
        m_state = StreamingState::Failed;
        return false;
    }

    const bool uploaded = chunk->uploadGpu(device, transferCommandPool);
    if (!uploaded) {
        m_state = StreamingState::Failed;
        return false;
    }

    m_gpuMemoryBytes = chunk->getGpuMemoryUsageBytes();
    m_state = StreamingState::Resident;
    return true;
}

bool StreamingRegion::activate(Terrain* terrain, FoliageSystem* foliageSystem) {
    if (m_state != StreamingState::Resident && m_state != StreamingState::Active) {
        return false;
    }

    TerrainChunk* chunk = getTerrainChunk();
    if (chunk) {
        chunk->setVisible(true);
        if (terrain && !m_isRegisteredInTerrain) {
            m_isRegisteredInTerrain = true;
        }
    }

    if (foliageSystem && !m_isRegisteredInFoliage && !m_foliageInstances.empty()) {
        foliageSystem->addInstances(m_foliageInstances);
        m_isRegisteredInFoliage = true;
    }

    m_state = StreamingState::Active;
    return true;
}

void StreamingRegion::deactivate(Terrain* terrain, FoliageSystem* foliageSystem) {
    (void)terrain;
    TerrainChunk* chunk = getTerrainChunk();
    if (chunk) {
        chunk->setVisible(false);
    }

    if (foliageSystem && m_isRegisteredInFoliage && !m_foliageInstanceIds.empty()) {
        foliageSystem->removeInstances(m_foliageInstanceIds);
        m_isRegisteredInFoliage = false;
    }

    if (m_state == StreamingState::Active) {
        m_state = StreamingState::Resident;
    }
}

void StreamingRegion::releaseGpu(Terrain* terrain, FoliageSystem* foliageSystem) {
    deactivate(terrain, foliageSystem);

    TerrainChunk* chunk = getTerrainChunk();
    if (chunk) {
        chunk->releaseGpu();
    }
    m_gpuMemoryBytes = 0;
}

void StreamingRegion::unloadCpu() {
    if (m_state == StreamingState::Active) {
        deactivate(nullptr, nullptr);
    }
    TerrainChunk* chunk = getTerrainChunk();
    if (chunk) {
        chunk->releaseGpu();
    }
    m_terrainChunk.reset();
    m_foliageInstances.clear();
    m_foliageInstanceIds.clear();
    m_isRegisteredInTerrain = false;
    m_isRegisteredInFoliage = false;
    m_cpuMemoryBytes = 0;
    m_gpuMemoryBytes = 0;
    m_state = StreamingState::Unloaded;
}

uint32_t StreamingRegion::getTriangleCount() const noexcept {
    uint32_t tris = 0;
    const TerrainChunk* chunk = getTerrainChunk();
    if (chunk) {
        tris += chunk->getTriangleCount();
    }
    return tris;
}

void StreamingRegion::generateFoliage(const HeightmapData& heightmap,
                                      const TerrainConfig& terrainConfig,
                                      const FoliageSystem* foliageSystem) {
    m_foliageInstances.clear();
    m_foliageInstanceIds.clear();

    // Standard foliage type IDs registered by FoliageRenderer:
    // 1: Tree (Pine), 2: Bush (Broadleaf), 3: Grass (Meadow), 4: SmallVegetation (Fern)
    uint32_t treeCount = 0;
    uint32_t bushCount = 0;
    uint32_t grassCount = 0;
    uint32_t fernCount = 0;

    switch (m_biome) {
        case RegionBiome::LushMeadow:
            treeCount = 3;
            bushCount = 8;
            grassCount = 65;
            fernCount = 10;
            break;
        case RegionBiome::PineForest:
            treeCount = 28;
            bushCount = 12;
            grassCount = 30;
            fernCount = 22;
            break;
        case RegionBiome::AlpineRidge:
            treeCount = 6;
            bushCount = 6;
            grassCount = 18;
            fernCount = 4;
            break;
        case RegionBiome::RockyPlateau:
            treeCount = 5;
            bushCount = 14;
            grassCount = 25;
            fernCount = 8;
            break;
        case RegionBiome::DenseWoodland:
            treeCount = 24;
            bushCount = 18;
            grassCount = 45;
            fernCount = 18;
            break;
        case RegionBiome::ValleyBasin:
            treeCount = 8;
            bushCount = 16;
            grassCount = 80;
            fernCount = 14;
            break;
    }

    const float minX = m_worldBounds.min.x + 1.0f;
    const float maxX = m_worldBounds.max.x - 1.0f;
    const float minZ = m_worldBounds.min.z + 1.0f;
    const float maxZ = m_worldBounds.max.z - 1.0f;

    if (minX >= maxX || minZ >= maxZ) {
        return;
    }

    // Deterministic pseudo-random seed based on region ID
    const uint32_t regionSeed = 10007u * (m_regionId + 1u) + 7919u;
    std::mt19937 rng(regionSeed);
    std::uniform_real_distribution<float> distX(minX, maxX);
    std::uniform_real_distribution<float> distZ(minZ, maxZ);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    const auto sampleTerrainAt = [&](float wx, float wz, glm::vec3& outPos, glm::vec3& outNormal) -> bool {
        const float halfW = terrainConfig.width * 0.5f;
        const float halfL = terrainConfig.length * 0.5f;
        if (wx < -halfW || wx > halfW || wz < -halfL || wz > halfL) {
            return false;
        }
        const float u = (wx + halfW) / terrainConfig.width;
        const float v = (wz + halfL) / terrainConfig.length;
        const float normH = heightmap.sampleBilinear(u, v);
        const float wy = terrainConfig.heightOffset + normH * terrainConfig.heightScale;

        // Bilinear normal calculation
        const float texelSizeX = 1.0f / static_cast<float>(std::max(1u, heightmap.getWidth() - 1));
        const float texelSizeZ = 1.0f / static_cast<float>(std::max(1u, heightmap.getHeight() - 1));
        const float hL = heightmap.sampleBilinear(std::max(0.0f, u - texelSizeX), v) * terrainConfig.heightScale;
        const float hR = heightmap.sampleBilinear(std::min(1.0f, u + texelSizeX), v) * terrainConfig.heightScale;
        const float hD = heightmap.sampleBilinear(u, std::max(0.0f, v - texelSizeZ)) * terrainConfig.heightScale;
        const float hU = heightmap.sampleBilinear(u, std::min(1.0f, v + texelSizeZ)) * terrainConfig.heightScale;

        const float worldSpacingX = terrainConfig.width / static_cast<float>(heightmap.getWidth());
        const float worldSpacingZ = terrainConfig.length / static_cast<float>(heightmap.getHeight());
        const glm::vec3 nX(2.0f * worldSpacingX, hR - hL, 0.0f);
        const glm::vec3 nZ(0.0f, hU - hD, 2.0f * worldSpacingZ);
        outNormal = glm::normalize(glm::cross(nZ, nX));
        outPos = glm::vec3(wx, wy, wz);
        return true;
    };

    uint32_t nextLocalId = (m_regionId + 1u) * 100000u;

    const auto spawnInstances = [&](uint32_t typeId, uint32_t count, float maxSlopeDeg, glm::vec3 scaleRange, bool alignNormal) {
        uint32_t placed = 0;
        for (uint32_t i = 0; i < count * 3 && placed < count; ++i) {
            const float wx = distX(rng);
            const float wz = distZ(rng);
            glm::vec3 pos;
            glm::vec3 normal;
            if (!sampleTerrainAt(wx, wz, pos, normal)) continue;

            const float slopeDeg = glm::degrees(std::acos(std::clamp(normal.y, -1.0f, 1.0f)));
            if (slopeDeg > maxSlopeDeg) continue;

            const float yaw = dist01(rng) * 2.0f * std::numbers::pi_v<float>;
            const float sFactor = glm::mix(scaleRange.x, scaleRange.y, dist01(rng));
            const glm::vec3 scale(sFactor);

            glm::mat4 rotMat(1.0f);
            if (alignNormal && normal.y < 0.999f) {
                const glm::vec3 up(0.0f, 1.0f, 0.0f);
                const glm::vec3 axis = glm::normalize(glm::cross(up, normal));
                rotMat = glm::rotate(glm::mat4(1.0f), std::acos(normal.y), axis);
                rotMat = glm::rotate(rotMat, yaw, up);
            } else {
                rotMat = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            }

            glm::mat4 model(1.0f);
            model = glm::translate(model, pos);
            model = model * rotMat;
            model = glm::scale(model, scale);

            const uint32_t instanceId = ++nextLocalId;
            FoliageInstance inst{};
            inst.instanceId = instanceId;
            inst.typeId = typeId;
            inst.randomSeed = static_cast<uint32_t>(rng());
            inst.position = pos;
            inst.rotation = glm::vec3(0.0f, yaw, 0.0f);
            inst.scale = scale;
            inst.transform = model;
            inst.terrainHeight = pos.y;
            inst.surfaceNormal = normal;
            inst.worldBounds = AABB{pos - glm::vec3(2.0f) * sFactor, pos + glm::vec3(2.0f, 6.0f, 2.0f) * sFactor};
            inst.worldSphere = BoundingSphere{pos + glm::vec3(0.0f, 2.0f, 0.0f) * sFactor, 3.5f * sFactor};
            inst.currentLod = 0;
            inst.isVisible = true;

            m_foliageInstances.push_back(inst);
            m_foliageInstanceIds.push_back(instanceId);
            ++placed;
        }
    };

    (void)foliageSystem;
    spawnInstances(1u, treeCount, 38.0f, glm::vec3(0.85f, 1.35f, 0.0f), false);
    spawnInstances(2u, bushCount, 45.0f, glm::vec3(0.75f, 1.25f, 0.0f), true);
    spawnInstances(3u, grassCount, 45.0f, glm::vec3(0.80f, 1.40f, 0.0f), true);
    spawnInstances(4u, fernCount, 42.0f, glm::vec3(0.70f, 1.20f, 0.0f), true);
}

} // namespace sidera
