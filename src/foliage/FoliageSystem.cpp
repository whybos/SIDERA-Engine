#include "foliage/FoliageSystem.h"
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <unordered_set>

namespace sidera {

uint32_t FoliageSystem::registerType(const FoliageType& type) {
    uint32_t id = type.typeId;
    if (id == 0) {
        id = static_cast<uint32_t>(m_types.size() + 1);
    }

    FoliageType entry = type;
    entry.typeId = id;

    const size_t index = m_types.size();
    m_types.push_back(entry);
    m_typeIndexMap[id] = index;

    SIDERA_LOG_INFO(
        "Foliage",
        "Registered FoliageType [ID: {}] '{}' (Category: {}, CastShadows: {}).",
        id,
        entry.name,
        getFoliageCategoryName(entry.category),
        entry.castShadows
    );

    return id;
}

FoliageType* FoliageSystem::getType(uint32_t typeId) noexcept {
    const auto it = m_typeIndexMap.find(typeId);
    if (it != m_typeIndexMap.end() && it->second < m_types.size()) {
        return &m_types[it->second];
    }
    return nullptr;
}

const FoliageType* FoliageSystem::getType(uint32_t typeId) const noexcept {
    const auto it = m_typeIndexMap.find(typeId);
    if (it != m_typeIndexMap.end() && it->second < m_types.size()) {
        return &m_types[it->second];
    }
    return nullptr;
}

bool FoliageSystem::addInstance(uint32_t typeId, float worldX, float worldZ, const Terrain& terrain, uint32_t seed) {
    const FoliageType* type = getType(typeId);
    if (!type) {
        return false;
    }

    // 1. Terrain containment check
    if (!terrain.containsWorldPoint(worldX, worldZ)) {
        return false;
    }

    // 2. Terrain height query
    bool outOfBounds = false;
    const float y = terrain.getHeightAt(worldX, worldZ, &outOfBounds);
    if (outOfBounds) {
        return false;
    }

    // 3. Elevation limits
    if (y < type->minElevation || y > type->maxElevation) {
        return false;
    }

    // 4. Terrain slope normal query and angle limits
    const glm::vec3 normal = terrain.getNormalAt(worldX, worldZ);
    const float dotUp = glm::clamp(normal.y, -1.0f, 1.0f);
    const float slopeAngleDeg = glm::degrees(std::acos(dotUp));
    if (slopeAngleDeg > type->maxSlopeAngleDegrees) {
        return false;
    }

    // 5. Deterministic pseudo-random variation from seed
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    const float yaw = dist01(rng) * 2.0f * std::numbers::pi_v<float>;
    const float sFrac = dist01(rng);
    const glm::vec3 scale = glm::mix(type->minScale, type->maxScale, sFrac);

    // 6. Orientation calculation
    glm::mat4 rotationMat(1.0f);
    if (type->alignToTerrainNormal && dotUp < 0.999f) {
        // Rotate upright Y to match terrain surface normal, then rotate by random yaw
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 axis = glm::normalize(glm::cross(up, normal));
        const float angle = std::acos(dotUp);
        rotationMat = glm::rotate(glm::mat4(1.0f), angle, axis);
        rotationMat = glm::rotate(rotationMat, yaw, up);
    } else {
        // Pure vertical orientation with random yaw around Y
        rotationMat = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // 7. World transform matrix: Translation * Rotation * Scale
    const glm::vec3 worldPos(worldX, y, worldZ);
    glm::mat4 model(1.0f);
    model = glm::translate(model, worldPos);
    model = model * rotationMat;
    model = glm::scale(model, scale);

    // 8. World-space bounding volumes
    const float maxScaleComponent = std::max({scale.x, scale.y, scale.z});
    const BoundingSphere worldSphere(
        worldPos + glm::vec3(rotationMat * glm::vec4(type->localSphere.center * scale, 0.0f)),
        type->localSphere.radius * maxScaleComponent
    );

    const glm::vec3 aabbHalfExt = (type->localBounds.max - type->localBounds.min) * 0.5f * scale;
    const glm::vec3 aabbCenter = worldPos + (type->localBounds.min + type->localBounds.max) * 0.5f * scale;
    const AABB worldBounds(aabbCenter - aabbHalfExt, aabbCenter + aabbHalfExt);

    // 9. Store concrete instance
    FoliageInstance inst{};
    inst.instanceId = m_nextInstanceId++;
    inst.typeId = typeId;
    inst.randomSeed = seed;
    inst.position = worldPos;
    inst.rotation = glm::vec3(0.0f, yaw, 0.0f);
    inst.scale = scale;
    inst.transform = model;
    inst.terrainHeight = y;
    inst.surfaceNormal = normal;
    inst.worldBounds = worldBounds;
    inst.worldSphere = worldSphere;
    inst.currentLod = 0;
    inst.isVisible = true;

    m_instances.push_back(inst);
    return true;
}

uint32_t FoliageSystem::generateFoliageField(
    uint32_t typeId,
    uint32_t count,
    float minX, float maxX,
    float minZ, float maxZ,
    const Terrain& terrain,
    uint32_t seed
) {
    if (count == 0) return 0;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> distX(minX, maxX);
    std::uniform_real_distribution<float> distZ(minZ, maxZ);

    uint32_t placed = 0;
    const uint32_t maxAttempts = count * 4;

    for (uint32_t attempt = 0; attempt < maxAttempts && placed < count; ++attempt) {
        const float x = distX(rng);
        const float z = distZ(rng);
        const uint32_t instSeed = static_cast<uint32_t>(rng());

        if (addInstance(typeId, x, z, terrain, instSeed)) {
            ++placed;
        }
    }

    const FoliageType* type = getType(typeId);
    SIDERA_LOG_INFO(
        "Foliage",
        "Generated foliage field for '{}' [ID: {}]: placed {}/{} instances in [{:.1f}, {:.1f}] x [{:.1f}, {:.1f}].",
        type ? type->name : "Unknown",
        typeId,
        placed,
        count,
        minX, maxX, minZ, maxZ
    );

    return placed;
}

void FoliageSystem::addInstance(const FoliageInstance& instance) {
    m_instances.push_back(instance);
    if (instance.instanceId >= m_nextInstanceId) {
        m_nextInstanceId = instance.instanceId + 1;
    }
}

uint32_t FoliageSystem::addInstances(const std::vector<FoliageInstance>& instances) {
    if (instances.empty()) return 0;
    m_instances.reserve(m_instances.size() + instances.size());
    for (const auto& inst : instances) {
        addInstance(inst);
    }
    return static_cast<uint32_t>(instances.size());
}

void FoliageSystem::removeInstances(const std::vector<uint32_t>& instanceIds) {
    if (instanceIds.empty()) return;
    std::unordered_set<uint32_t> idSet(instanceIds.begin(), instanceIds.end());
    std::erase_if(m_instances, [&idSet](const FoliageInstance& inst) {
        return idSet.contains(inst.instanceId);
    });
}

void FoliageSystem::removeInstance(uint32_t instanceId) {
    const auto it = std::find_if(m_instances.begin(), m_instances.end(), [instanceId](const FoliageInstance& i) {
        return i.instanceId == instanceId;
    });
    if (it != m_instances.end()) {
        m_instances.erase(it);
    }
}

void FoliageSystem::clearInstances() noexcept {
    m_instances.clear();
    m_stats = FoliageStatistics{};
}

bool FoliageSystem::update(float deltaTime, const Camera& camera, float viewportHeight, const LODManager& lodManager) {
    // 1. Advance continuous wind animation time
    if (m_config.windEnabled) {
        m_windTime += deltaTime;
    }

    // 2. Clear per-frame statistics
    m_stats.totalInstances = static_cast<uint32_t>(m_instances.size());
    m_stats.totalTypes = static_cast<uint32_t>(m_types.size());
    m_stats.visibleInstances = 0;
    m_stats.culledInstances = 0;
    m_stats.totalBatches = 0;
    m_stats.totalDrawCalls = 0;
    m_stats.totalTriangles = 0;
    m_stats.visibleTriangles = 0;
    m_stats.treeInstanceCount = 0;
    m_stats.grassInstanceCount = 0;
    m_stats.bushInstanceCount = 0;
    m_stats.smallVegetationCount = 0;
    m_stats.lod0Count = 0;
    m_stats.lod1Count = 0;
    m_stats.lod2Count = 0;
    m_stats.lod3Count = 0;
    m_stats.windTime = m_windTime;

    bool anyChanged = false;

    if (!m_config.enabled) {
        for (auto& inst : m_instances) {
            if (inst.isVisible) {
                inst.isVisible = false;
                anyChanged = true;
            }
        }
        return anyChanged;
    }

    // 3. Camera context for frustum and LOD evaluation
    const glm::vec3 camPos = camera.getPosition();
    const Frustum frustum = Frustum::fromViewProj(camera.getProjectionMatrix() * camera.getViewMatrix());
    const float fovRad = glm::radians(camera.getFovDegrees());
    const float cotHalfFov = 1.0f / std::tan(fovRad * 0.5f);

    for (size_t i = 0; i < m_instances.size(); ++i) {
        auto& inst = m_instances[i];
        const uint32_t prevLod = inst.currentLod;
        const bool prevVisible = inst.isVisible;
        const FoliageType* type = getType(inst.typeId);
        if (!type) continue;

        // Account category
        switch (type->category) {
            case FoliageCategory::Tree:            ++m_stats.treeInstanceCount; break;
            case FoliageCategory::Grass:           ++m_stats.grassInstanceCount; break;
            case FoliageCategory::Bush:            ++m_stats.bushInstanceCount; break;
            case FoliageCategory::SmallVegetation: ++m_stats.smallVegetationCount; break;
        }

        const uint64_t baseTris = type->mesh ? (type->mesh->getIndexCount() / 3ull) : 0ull;
        m_stats.totalTriangles += baseTris;

        // Distance culling check
        const float dist = glm::length(inst.position - camPos);
        if (dist > m_config.maxDrawDistance || dist > type->maxDistance) {
            inst.isVisible = false;
            ++m_stats.culledInstances;
            if (prevVisible != inst.isVisible) anyChanged = true;
            continue;
        }

        // Frustum culling check
        if (!frustum.intersectsSphere(inst.worldSphere.center, inst.worldSphere.radius)) {
            inst.isVisible = false;
            ++m_stats.culledInstances;
            if (prevVisible != inst.isVisible) anyChanged = true;
            continue;
        }

        inst.isVisible = true;
        ++m_stats.visibleInstances;

        // Dynamic LOD selection with hysteresis (Milestone 19)
        if (type->lodGroup && type->lodGroup->getLevelCount() > 0) {
            const float projScreenSize = (2.0f * inst.worldSphere.radius * cotHalfFov / std::max(0.1f, dist)) * (viewportHeight * 0.5f);
            const uint32_t chosenLod = lodManager.selectLOD(*type->lodGroup, dist, projScreenSize, inst.currentLod);
            inst.currentLod = chosenLod;

            const Mesh* activeMesh = type->lodGroup->getLevel(chosenLod).mesh;
            const uint64_t activeTris = activeMesh ? (activeMesh->getIndexCount() / 3ull) : baseTris;
            m_stats.visibleTriangles += activeTris;
        } else {
            inst.currentLod = 0;
            m_stats.visibleTriangles += baseTris;
        }

        if (inst.currentLod != prevLod || inst.isVisible != prevVisible) {
            anyChanged = true;
        }

        // Account LOD tier counts
        switch (inst.currentLod) {
            case 0: ++m_stats.lod0Count; break;
            case 1: ++m_stats.lod1Count; break;
            case 2: ++m_stats.lod2Count; break;
            case 3: ++m_stats.lod3Count; break;
            default: break;
        }
    }

    return anyChanged;
}

void FoliageSystem::populateBatches(InstanceBatchManager& batchManager) const {
    if (!m_config.enabled) {
        return;
    }

    for (const auto& inst : m_instances) {
        if (!inst.isVisible) {
            continue;
        }

        const FoliageType* type = getType(inst.typeId);
        if (!type) continue;

        // Resolve active LOD mesh
        const Mesh* activeMesh = nullptr;
        if (type->lodGroup && inst.currentLod < type->lodGroup->getLevelCount()) {
            activeMesh = type->lodGroup->getLevel(inst.currentLod).mesh;
        }
        if (!activeMesh) {
            activeMesh = type->mesh;
        }
        if (!activeMesh) continue;

        // Setup Material with alphaCutoff and wind flag
        Material mat = type->material;
        mat.flags |= (m_config.windEnabled ? 1u : 0u);

        // Setup Instance
        Instance engineInst{};
        engineInst.transform = inst.transform;
        engineInst.worldAabb = inst.worldBounds;
        engineInst.worldSphere = inst.worldSphere;
        engineInst.position = inst.position;
        engineInst.flags = m_config.windEnabled ? 1u : 0u;
        engineInst.customId = inst.instanceId;

        batchManager.addInstance(activeMesh, mat, engineInst);
    }
}

} // namespace sidera
