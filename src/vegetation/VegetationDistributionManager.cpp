#include "vegetation/VegetationDistributionManager.h"
#include "core/Log.h"
#include "foliage/FoliageSystem.h"
#include "partition/WorldPartitionGrid.h"
#include "terrain/Terrain.h"
#include "worldgen/WorldSeed.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace sidera {

namespace {

[[nodiscard]] constexpr float smoothstep01(float edge0, float edge1, float x) noexcept {
    const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-5f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] constexpr float rangeSuitability(float val, float minV, float maxV, float feather = 0.08f) noexcept {
    if (val < minV - feather || val > maxV + feather) return 0.0f;
    const float low  = smoothstep01(minV - feather, minV, val);
    const float high = 1.0f - smoothstep01(maxV, maxV + feather, val);
    return low * high;
}

[[nodiscard]] inline float preferredSuitability(float val, float pref, float tol) noexcept {
    const float d = std::abs(val - pref);
    if (d >= tol * 1.5f) return 0.0f;
    return 1.0f - smoothstep01(tol * 0.35f, tol * 1.5f, d);
}

} // namespace

void VegetationDistributionManager::init(const VegetationDistributionConfig& config,
                                         const WorldGenerator* worldGen,
                                         const Terrain* terrain) {
    m_config = config;
    m_worldGen = worldGen;
    m_terrain = terrain;
    clearCache();
    m_stats.reset();

    if (m_species.empty()) {
        registerDefaultSpecies();
    }

    if (m_worldGen) {
        const auto& wCfg = m_worldGen->getConfig();
        const float halfW = wCfg.worldWidth * 0.5f;
        const float halfL = wCfg.worldLength * 0.5f;
        m_densityMap.generate(*m_worldGen, -halfW, halfW, -halfL, halfL,
                              m_config.densityMapResolution, m_config.densityMultiplier);
    }

    m_stats.totalSpeciesCount = static_cast<uint32_t>(m_species.size());
    SIDERA_LOG_INFO("Vegetation", "VegetationDistributionManager initialized ({} species, seed={}).",
                    m_species.size(), m_config.worldSeed);
}

void VegetationDistributionManager::registerDefaultSpecies() {
    m_species.clear();

    // 1. Pine (Temperate, Dense Forest, Alpine)
    {
        VegetationSpecies s{};
        s.speciesId = 1; s.name = "Pine"; s.category = VegetationCategory::Tree; s.foliageTypeId = 1;
        s.rule.allowedBiomeMask = biomeBit(BiomeType::TemperateForest) | biomeBit(BiomeType::DenseForest) | biomeBit(BiomeType::Alpine);
        s.rule.minAltitude = 0.25f; s.rule.maxAltitude = 0.72f; s.rule.maxSlope = 0.38f;
        s.rule.preferredTemperature = 0.42f; s.rule.temperatureTolerance = 0.32f;
        s.rule.preferredMoisture = 0.55f; s.rule.moistureTolerance = 0.35f;
        s.rule.minSpacing = 4.2f; s.baseDensity = 0.65f; s.minScale = 0.85f; s.maxScale = 1.45f;
        s.alignToTerrainNormal = 0.08f; s.clusterRadius = 10.0f; s.clusterStrength = 0.55f; s.placementPriority = 100;
        s.algorithm = DistributionAlgorithm::Hybrid;
        s.debugColor = glm::vec4(0.10f, 0.50f, 0.18f, 1.0f);
        m_species.push_back(s);
    }
    // 2. Oak (Temperate, Dense Forest, Grassland)
    {
        VegetationSpecies s{};
        s.speciesId = 2; s.name = "Oak"; s.category = VegetationCategory::Tree; s.foliageTypeId = 1;
        s.rule.allowedBiomeMask = biomeBit(BiomeType::TemperateForest) | biomeBit(BiomeType::DenseForest) | biomeBit(BiomeType::Grassland);
        s.rule.minAltitude = 0.22f; s.rule.maxAltitude = 0.60f; s.rule.maxSlope = 0.32f;
        s.rule.preferredTemperature = 0.55f; s.rule.temperatureTolerance = 0.30f;
        s.rule.preferredMoisture = 0.58f; s.rule.moistureTolerance = 0.32f;
        s.rule.minSpacing = 4.8f; s.baseDensity = 0.55f; s.minScale = 0.90f; s.maxScale = 1.40f;
        s.alignToTerrainNormal = 0.05f; s.clusterRadius = 9.0f; s.clusterStrength = 0.50f; s.placementPriority = 95;
        s.algorithm = DistributionAlgorithm::PoissonDisk;
        s.debugColor = glm::vec4(0.18f, 0.62f, 0.15f, 1.0f);
        m_species.push_back(s);
    }
    // 3. Birch (Temperate, Wetlands, Grassland)
    {
        VegetationSpecies s{};
        s.speciesId = 3; s.name = "Birch"; s.category = VegetationCategory::Tree; s.foliageTypeId = 1;
        s.rule.allowedBiomeMask = biomeBit(BiomeType::TemperateForest) | biomeBit(BiomeType::Wetlands) | biomeBit(BiomeType::Grassland);
        s.rule.minAltitude = 0.22f; s.rule.maxAltitude = 0.55f; s.rule.maxSlope = 0.30f;
        s.rule.preferredTemperature = 0.48f; s.rule.temperatureTolerance = 0.28f;
        s.rule.preferredMoisture = 0.65f; s.rule.moistureTolerance = 0.30f;
        s.rule.minSpacing = 4.0f; s.baseDensity = 0.45f; s.minScale = 0.80f; s.maxScale = 1.25f;
        s.alignToTerrainNormal = 0.08f; s.placementPriority = 90;
        s.debugColor = glm::vec4(0.75f, 0.85f, 0.55f, 1.0f);
        m_species.push_back(s);
    }
    // 4. Palm (Tropical, Coastal)
    {
        VegetationSpecies s{};
        s.speciesId = 4; s.name = "Palm"; s.category = VegetationCategory::Tree; s.foliageTypeId = 1;
        s.rule.allowedBiomeMask = biomeBit(BiomeType::Tropical) | biomeBit(BiomeType::Coastal);
        s.rule.minAltitude = 0.20f; s.rule.maxAltitude = 0.42f; s.rule.maxSlope = 0.28f;
        s.rule.preferredTemperature = 0.80f; s.rule.temperatureTolerance = 0.28f;
        s.rule.preferredMoisture = 0.65f; s.rule.moistureTolerance = 0.35f;
        s.rule.sandAffinity = 0.9f; s.rule.minSpacing = 4.5f; s.baseDensity = 0.50f;
        s.placementPriority = 85;
        s.debugColor = glm::vec4(0.35f, 0.85f, 0.25f, 1.0f);
        m_species.push_back(s);
    }
    // 5. Forest Bush (Forest, Grassland, Wetlands, Tropical)
    {
        VegetationSpecies s{};
        s.speciesId = 5; s.name = "ForestBush"; s.category = VegetationCategory::Bush; s.foliageTypeId = 2;
        s.rule.allowedBiomeMask = biomeBit(BiomeType::TemperateForest) | biomeBit(BiomeType::DenseForest) |
                                  biomeBit(BiomeType::Grassland) | biomeBit(BiomeType::Wetlands) | biomeBit(BiomeType::Tropical);
        s.rule.minAltitude = 0.22f; s.rule.maxAltitude = 0.65f; s.rule.maxSlope = 0.45f;
        s.rule.preferredTemperature = 0.52f; s.rule.temperatureTolerance = 0.38f;
        s.rule.preferredMoisture = 0.55f; s.rule.moistureTolerance = 0.40f;
        s.rule.minSpacing = 2.2f; s.baseDensity = 0.60f; s.minScale = 0.65f; s.maxScale = 1.25f;
        s.alignToTerrainNormal = 0.35f; s.clusterRadius = 6.0f; s.clusterStrength = 0.60f; s.placementPriority = 60;
        s.algorithm = DistributionAlgorithm::Clustered;
        s.debugColor = glm::vec4(0.25f, 0.72f, 0.30f, 1.0f);
        m_species.push_back(s);
    }
    // 6. Dry Bush (Desert, Coastal, Alpine, Tundra)
    {
        VegetationSpecies s{};
        s.speciesId = 6; s.name = "DryBush"; s.category = VegetationCategory::Bush; s.foliageTypeId = 2;
        s.rule.allowedBiomeMask = biomeBit(BiomeType::Desert) | biomeBit(BiomeType::Coastal) |
                                  biomeBit(BiomeType::Alpine) | biomeBit(BiomeType::Tundra);
        s.rule.minAltitude = 0.20f; s.rule.maxAltitude = 0.75f; s.rule.maxSlope = 0.48f;
        s.rule.preferredTemperature = 0.72f; s.rule.temperatureTolerance = 0.45f;
        s.rule.preferredMoisture = 0.22f; s.rule.moistureTolerance = 0.35f;
        s.rule.sandAffinity = 0.95f; s.rule.minSpacing = 2.6f; s.baseDensity = 0.40f;
        s.alignToTerrainNormal = 0.35f; s.placementPriority = 55;
        s.debugColor = glm::vec4(0.80f, 0.65f, 0.25f, 1.0f);
        m_species.push_back(s);
    }
    // 7. Meadow Grass (Grassland, Forest, Wetlands, Alpine, Tropical)
    {
        VegetationSpecies s{};
        s.speciesId = 7; s.name = "MeadowGrass"; s.category = VegetationCategory::Grass; s.foliageTypeId = 3;
        s.rule.allowedBiomeMask = biomeBit(BiomeType::Grassland) | biomeBit(BiomeType::TemperateForest) |
                                  biomeBit(BiomeType::DenseForest) | biomeBit(BiomeType::Wetlands) |
                                  biomeBit(BiomeType::Alpine) | biomeBit(BiomeType::Tropical);
        s.rule.minAltitude = 0.22f; s.rule.maxAltitude = 0.68f; s.rule.maxSlope = 0.42f;
        s.rule.preferredTemperature = 0.52f; s.rule.temperatureTolerance = 0.42f;
        s.rule.preferredMoisture = 0.50f; s.rule.moistureTolerance = 0.42f;
        s.rule.minSpacing = 1.4f; s.rule.treeAvoidanceRadius = 1.2f;
        s.baseDensity = 0.85f; s.minScale = 0.65f; s.maxScale = 1.35f;
        s.alignToTerrainNormal = 0.85f; s.placementPriority = 20;
        s.algorithm = DistributionAlgorithm::JitteredGrid;
        s.debugColor = glm::vec4(0.45f, 0.92f, 0.25f, 1.0f);
        m_species.push_back(s);
    }
    // 8. Fern / Small Vegetation (Forest, Wetlands, Tropical, Tundra)
    {
        VegetationSpecies s{};
        s.speciesId = 8; s.name = "Fern"; s.category = VegetationCategory::SmallVegetation; s.foliageTypeId = 4;
        s.rule.allowedBiomeMask = biomeBit(BiomeType::TemperateForest) | biomeBit(BiomeType::DenseForest) |
                                  biomeBit(BiomeType::Wetlands) | biomeBit(BiomeType::Tropical) | biomeBit(BiomeType::Tundra);
        s.rule.minAltitude = 0.22f; s.rule.maxAltitude = 0.65f; s.rule.maxSlope = 0.45f;
        s.rule.preferredTemperature = 0.48f; s.rule.temperatureTolerance = 0.38f;
        s.rule.preferredMoisture = 0.65f; s.rule.moistureTolerance = 0.38f;
        s.rule.minSpacing = 1.8f; s.baseDensity = 0.55f; s.minScale = 0.60f; s.maxScale = 1.20f;
        s.alignToTerrainNormal = 0.70f; s.placementPriority = 30;
        s.algorithm = DistributionAlgorithm::WeightedRandom;
        s.debugColor = glm::vec4(0.15f, 0.75f, 0.55f, 1.0f);
        m_species.push_back(s);
    }

    // Sort by placementPriority descending so trees are placed before bushes and grass
    std::sort(m_species.begin(), m_species.end(), [](const VegetationSpecies& a, const VegetationSpecies& b) {
        return a.placementPriority > b.placementPriority;
    });
    m_stats.totalSpeciesCount = static_cast<uint32_t>(m_species.size());
}

uint32_t VegetationDistributionManager::registerSpecies(const VegetationSpecies& species) {
    m_species.push_back(species);
    std::sort(m_species.begin(), m_species.end(), [](const VegetationSpecies& a, const VegetationSpecies& b) {
        return a.placementPriority > b.placementPriority;
    });
    m_stats.totalSpeciesCount = static_cast<uint32_t>(m_species.size());
    return species.speciesId;
}

void VegetationDistributionManager::clearSpecies() {
    m_species.clear();
    m_stats.totalSpeciesCount = 0;
}

void VegetationDistributionManager::addExclusionZone(const VegetationExclusionZone& zone) {
    m_exclusionZones.push_back(zone);
}

void VegetationDistributionManager::clearExclusionZones() {
    m_exclusionZones.clear();
}

bool VegetationDistributionManager::isExcluded(float worldX, float worldZ) const noexcept {
    if (!m_config.enableExclusionZones) return false;
    for (const auto& z : m_exclusionZones) {
        if (z.contains(worldX, worldZ)) return true;
    }
    return false;
}

float VegetationDistributionManager::evaluateSuitability(const VegetationSpecies& species,
                                                         const WorldPointSample& pt) noexcept {
    const VegetationRule& r = species.rule;

    // 1. Water exclusion
    if (r.avoidWater && (pt.riverMask > 0.35f || pt.lakeMask > 0.35f)) {
        return 0.0f;
    }

    // 2. Biome compatibility (with smooth secondary biome blending)
    const bool primOk = (r.allowedBiomeMask & biomeBit(pt.biome.primaryBiome)) != 0u;
    const bool secOk  = (r.allowedBiomeMask & biomeBit(pt.biome.secondaryBiome)) != 0u;
    float biomeScore = 0.0f;
    if (primOk && secOk)      biomeScore = 1.0f;
    else if (primOk)          biomeScore = 1.0f - pt.biome.blendWeight * 0.45f;
    else if (secOk)           biomeScore = pt.biome.blendWeight * 0.55f;
    else                      return 0.0f;

    // 3. Altitude, Slope, Temperature, Moisture
    const float altScore   = rangeSuitability(pt.normalizedHeight, r.minAltitude, r.maxAltitude, 0.06f);
    const float slopeScore = rangeSuitability(pt.biome.slope, r.minSlope, r.maxSlope, 0.08f);
    const float tempScore  = preferredSuitability(pt.biome.temperature, r.preferredTemperature, r.temperatureTolerance);
    const float moistScore = preferredSuitability(pt.biome.moisture, r.preferredMoisture, r.moistureTolerance);

    // 4. Surface material compatibility (R=Grass, G=Rock, B=Dirt, A=Sand)
    const glm::vec4& mw = pt.biome.materialWeights;
    float matScore = 1.0f - mw.g * r.rockPenalty - pt.biome.snowWeight * r.snowPenalty;
    matScore = std::clamp(matScore + mw.r * (r.grassAffinity * 0.25f) + mw.a * (r.sandAffinity * 0.35f), 0.05f, 1.0f);

    return std::clamp(biomeScore * altScore * slopeScore * tempScore * moistScore * matScore, 0.0f, 1.0f);
}

RegionVegetationResult VegetationDistributionManager::computeRegion(int32_t regionX, int32_t regionZ) const {
    const auto t0 = std::chrono::high_resolution_clock::now();
    RegionVegetationResult result{};
    result.regionX = regionX;
    result.regionZ = regionZ;

    if (!m_worldGen || m_species.empty()) {
        return result;
    }

    const float regSize = std::max(m_config.regionSize, 4.0f);
    const float originX = static_cast<float>(regionX) * regSize;
    const float originZ = static_cast<float>(regionZ) * regSize;
    const WorldSeed regSeed = WorldSeed(m_config.worldSeed).deriveRegion(regionX, regionZ);

    // Keep track of placed tree positions for cross-species spacing / canopy avoidance
    std::vector<glm::vec3> placedTrees; // (x, z, spacing)

    for (const auto& sp : m_species) {
        const WorldSeed spSeed = regSeed.deriveLayer(sp.speciesId * 37u + 7u);
        const float spacing = std::max(sp.rule.minSpacing, 0.5f);
        const uint32_t gridDim = std::max(1u, static_cast<uint32_t>(std::floor(regSize / spacing)));
        const float cellStep = regSize / static_cast<float>(gridDim);

        // Deterministic cluster center for Clustered / Hybrid species
        VegetationCluster cluster{};
        cluster.speciesId = sp.speciesId;
        cluster.radius = sp.clusterRadius;
        cluster.densityMultiplier = 1.0f + sp.clusterStrength;
        cluster.center = glm::vec2(
            originX + spSeed.hash2DToFloat(1, 2) * regSize,
            originZ + spSeed.hash2DToFloat(3, 4) * regSize
        );
        if (m_config.enableClustering && (sp.algorithm == DistributionAlgorithm::Clustered || sp.algorithm == DistributionAlgorithm::Hybrid)) {
            result.clusters.push_back(cluster);
        }

        std::vector<glm::vec2> spPlaced;
        for (uint32_t gz = 0; gz < gridDim; ++gz) {
            for (uint32_t gx = 0; gx < gridDim; ++gx) {
                if (result.placements.size() >= m_config.maxInstancesPerRegion) {
                    break;
                }
                ++result.candidatesEvaluated;

                const float jx = spSeed.hash2DToFloat(static_cast<int32_t>(gx * 2u), static_cast<int32_t>(gz * 2u));
                const float jz = spSeed.hash2DToFloat(static_cast<int32_t>(gx * 2u + 1u), static_cast<int32_t>(gz * 2u + 1u));
                const float wx = originX + (static_cast<float>(gx) + 0.1f + jx * 0.8f) * cellStep;
                const float wz = originZ + (static_cast<float>(gz) + 0.1f + jz * 0.8f) * cellStep;

                // Strict half-open region ownership [origin, origin + regSize) prevents boundary duplicates
                if (wx < originX || wx >= originX + regSize || wz < originZ || wz >= originZ + regSize) {
                    continue;
                }
                if (isExcluded(wx, wz)) {
                    continue;
                }
                if (m_terrain && !m_terrain->containsWorldPoint(wx, wz)) {
                    continue;
                }

                const WorldPointSample pt = m_worldGen->samplePoint(wx, wz);
                const float suit = evaluateSuitability(sp, pt);
                if (suit <= 0.05f) {
                    continue;
                }

                // Density & cluster modulation
                float prob = suit * sp.baseDensity * m_config.densityMultiplier;
                if (m_densityMap.isValid()) {
                    prob *= (0.35f + 0.65f * m_densityMap.sample(wx, wz));
                }
                if (m_config.enableClustering && (sp.algorithm == DistributionAlgorithm::Clustered || sp.algorithm == DistributionAlgorithm::Hybrid)) {
                    const float distToCluster = glm::distance(glm::vec2(wx, wz), cluster.center);
                    const float cFactor = 1.0f - smoothstep01(cluster.radius * 0.3f, cluster.radius * 1.4f, distToCluster);
                    prob *= (1.0f - sp.clusterStrength * 0.5f) + cFactor * sp.clusterStrength;
                }

                const float roll = spSeed.hash2DToFloat(static_cast<int32_t>(gx + 1000u), static_cast<int32_t>(gz + 1000u));
                if (roll > std::clamp(prob, 0.0f, 1.0f)) {
                    continue;
                }

                // Minimum spacing check (Poisson Disk & cross-species tree avoidance)
                bool tooClose = false;
                const float minDistSq = spacing * spacing;
                for (const auto& p : spPlaced) {
                    const float dx = p.x - wx;
                    const float dz = p.y - wz;
                    if (dx * dx + dz * dz < minDistSq) {
                        tooClose = true;
                        break;
                    }
                }
                if (!tooClose && sp.rule.treeAvoidanceRadius > 0.0f) {
                    const float avoidSq = sp.rule.treeAvoidanceRadius * sp.rule.treeAvoidanceRadius;
                    for (const auto& tr : placedTrees) {
                        const float dx = tr.x - wx;
                        const float dz = tr.y - wz;
                        if (dx * dx + dz * dz < avoidSq) {
                            tooClose = true;
                            break;
                        }
                    }
                }
                if (tooClose) {
                    continue;
                }

                // Sample exact terrain height & surface normal (invariant under Terrain LOD)
                const float worldY = m_terrain ? m_terrain->getHeightAt(wx, wz) : pt.worldHeight;
                const glm::vec3 normal = m_terrain ? m_terrain->getNormalAt(wx, wz) : glm::vec3(0.0f, 1.0f, 0.0f);

                const float scaleRoll = spSeed.hash2DToFloat(static_cast<int32_t>(gx + 2000u), static_cast<int32_t>(gz + 2000u));
                const float sVal = sp.minScale + scaleRoll * (sp.maxScale - sp.minScale);
                const float yawRoll = spSeed.hash2DToFloat(static_cast<int32_t>(gx + 3000u), static_cast<int32_t>(gz + 3000u));
                const float yaw = sp.randomRotation ? (yawRoll * 2.0f * std::numbers::pi_v<float>) : 0.0f;

                const float pitch = std::asin(std::clamp(normal.z, -0.5f, 0.5f)) * sp.alignToTerrainNormal;
                const float rollAngle = -std::asin(std::clamp(normal.x, -0.5f, 0.5f)) * sp.alignToTerrainNormal;

                glm::mat4 model(1.0f);
                model = glm::translate(model, glm::vec3(wx, worldY, wz));
                model = glm::rotate(model, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                model = glm::rotate(model, pitch, glm::vec3(1.0f, 0.0f, 0.0f));
                model = glm::rotate(model, rollAngle, glm::vec3(0.0f, 0.0f, 1.0f));
                model = glm::scale(model, glm::vec3(sVal));

                FoliageInstance inst{};
                inst.typeId = sp.foliageTypeId;
                inst.randomSeed = spSeed.deriveRegion(static_cast<int32_t>(gx), static_cast<int32_t>(gz)).toUint32();
                inst.position = glm::vec3(wx, worldY, wz);
                inst.rotation = glm::vec3(yaw, pitch, rollAngle);
                inst.scale = glm::vec3(sVal);
                inst.transform = model;
                inst.terrainHeight = worldY;
                inst.surfaceNormal = normal;
                inst.worldBounds.min = inst.position - glm::vec3(sVal);
                inst.worldBounds.max = inst.position + glm::vec3(sVal * 2.5f);
                inst.worldSphere.center = inst.position + glm::vec3(0.0f, sVal, 0.0f);
                inst.worldSphere.radius = sVal * 2.0f;

                VegetationPlacement pl{};
                pl.foliageInstance = inst;
                pl.speciesId = sp.speciesId;
                pl.category = sp.category;
                pl.biome = pt.biome.primaryBiome;
                pl.suitabilityScore = suit;
                const CellCoord cCoord = WorldPartitionGrid::worldToCell(inst.position, m_config.partitionCellSize);
                pl.ownerCellId = WorldPartitionGrid::computeCellId(cCoord.x, cCoord.z);

                result.placements.push_back(pl);
                spPlaced.emplace_back(wx, wz);
                if (sp.category == VegetationCategory::Tree) {
                    placedTrees.emplace_back(wx, wz, spacing);
                }
            }
        }
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    result.generationTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    return result;
}

const RegionVegetationResult& VegetationDistributionManager::generateRegion(int32_t regionX, int32_t regionZ) {
    const uint64_t key = packRegionKey(regionX, regionZ);
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_regionCache.find(key);
        if (it != m_regionCache.end()) {
            return *it->second;
        }
    }

    auto res = std::make_unique<RegionVegetationResult>(computeRegion(regionX, regionZ));
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto [it, inserted] = m_regionCache.emplace(key, std::move(res));
    if (inserted) {
        const auto& r = *it->second;
        m_stats.totalCandidates += r.candidatesEvaluated;
        m_stats.acceptedPlacements += r.placements.size();
        m_stats.rejectedPlacements += (r.candidatesEvaluated - r.placements.size());
        m_stats.totalClusters += r.clusters.size();
        m_stats.totalVegetationInstances += r.placements.size();
        m_stats.lastRegionGenerationTimeMs = r.generationTimeMs;
        m_stats.distributionGenerationTimeMs += r.generationTimeMs;
        m_stats.cpuDistributionMemoryBytes += r.placements.size() * sizeof(VegetationPlacement);

        double sumSuit = 0.0;
        for (const auto& p : r.placements) {
            sumSuit += p.suitabilityScore;
            switch (p.category) {
                case VegetationCategory::Tree:            ++m_stats.totalTreeInstances; break;
                case VegetationCategory::Bush:            ++m_stats.totalBushInstances; break;
                case VegetationCategory::Grass:           ++m_stats.totalGrassInstances; break;
                case VegetationCategory::SmallVegetation: ++m_stats.totalSmallVegetationInstances; break;
            }
            const size_t bIdx = static_cast<size_t>(p.biome);
            if (bIdx < m_stats.instanceCountPerBiome.size()) {
                ++m_stats.instanceCountPerBiome[bIdx];
            }
        }
        if (!r.placements.empty()) {
            m_stats.averageSuitabilityScore = static_cast<float>(sumSuit / static_cast<double>(r.placements.size()));
        }
        if (m_stats.distributionGenerationTimeMs > 0.001f) {
            m_stats.placementThroughputPerSec = static_cast<float>(m_stats.acceptedPlacements) /
                                                (m_stats.distributionGenerationTimeMs * 0.001f);
        }
    }
    return *it->second;
}

const RegionVegetationResult& VegetationDistributionManager::regenerateRegion(int32_t regionX, int32_t regionZ) {
    const uint64_t key = packRegionKey(regionX, regionZ);
    auto res = std::make_unique<RegionVegetationResult>(computeRegion(regionX, regionZ));
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_regionCache[key] = std::move(res);
    return *m_regionCache[key];
}

void VegetationDistributionManager::clearCache() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_regionCache.clear();
}

uint64_t VegetationDistributionManager::distributeWorld(int32_t minRegionX, int32_t maxRegionX,
                                                        int32_t minRegionZ, int32_t maxRegionZ,
                                                        FoliageSystem* foliageSystem) {
    uint64_t totalAdded = 0;
    std::vector<FoliageInstance> batchInstances;

    for (int32_t rz = minRegionZ; rz <= maxRegionZ; ++rz) {
        for (int32_t rx = minRegionX; rx <= maxRegionX; ++rx) {
            const auto& region = generateRegion(rx, rz);
            for (const auto& pl : region.placements) {
                batchInstances.push_back(pl.foliageInstance);
            }
            totalAdded += region.placements.size();
        }
    }

    if (foliageSystem && !batchInstances.empty()) {
        foliageSystem->addInstances(batchInstances);
    }
    return totalAdded;
}

} // namespace sidera
