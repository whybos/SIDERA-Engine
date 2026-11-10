#pragma once

#include "foliage/FoliageBatch.h"
#include "foliage/FoliageConfig.h"
#include "foliage/FoliageInstance.h"
#include "foliage/FoliageStats.h"
#include "foliage/FoliageType.h"
#include "lod/LODManager.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "renderer/InstanceBatch.h"
#include "terrain/Terrain.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace sidera {

// Central management authority for foliage types, placed instances, LOD transitions, and instancing
class FoliageSystem {
public:
    FoliageSystem() = default;
    ~FoliageSystem() = default;

    FoliageSystem(const FoliageSystem&) = delete;
    FoliageSystem& operator=(const FoliageSystem&) = delete;
    FoliageSystem(FoliageSystem&&) noexcept = default;
    FoliageSystem& operator=(FoliageSystem&&) noexcept = default;

    // Type registration
    uint32_t registerType(const FoliageType& type);
    [[nodiscard]] FoliageType* getType(uint32_t typeId) noexcept;
    [[nodiscard]] const FoliageType* getType(uint32_t typeId) const noexcept;
    [[nodiscard]] const std::vector<FoliageType>& getTypes() const noexcept { return m_types; }

    // Instance management
    bool addInstance(uint32_t typeId, float worldX, float worldZ, const Terrain& terrain, uint32_t seed);
    uint32_t generateFoliageField(
        uint32_t typeId,
        uint32_t count,
        float minX, float maxX,
        float minZ, float maxZ,
        const Terrain& terrain,
        uint32_t seed
    );
    void removeInstance(uint32_t instanceId);
    void clearInstances() noexcept;

    // Runtime update: advances wind, evaluates camera distance/screen-space LOD, tests frustum culling. Returns true if visibility or LOD changed.
    bool update(float deltaTime, const Camera& camera, float viewportHeight, const LODManager& lodManager);

    // Integrates with Milestone 16 GPU Instancing
    void populateBatches(InstanceBatchManager& batchManager) const;

    // Accessors and runtime toggles
    [[nodiscard]] const FoliageConfiguration& getConfig() const noexcept { return m_config; }
    [[nodiscard]] FoliageConfiguration& getConfig() noexcept { return m_config; }
    void setConfig(const FoliageConfiguration& config) noexcept { m_config = config; }
    void setEnabled(bool enabled) noexcept { m_config.enabled = enabled; }
    void toggleEnabled() noexcept { m_config.enabled = !m_config.enabled; }
    void setWindEnabled(bool enabled) noexcept { m_config.windEnabled = enabled; }
    void toggleWind() noexcept { m_config.windEnabled = !m_config.windEnabled; }
    void setWindStrength(float strength) noexcept { m_config.wind.strength = strength; }
    void setWindSpeed(float speed) noexcept { m_config.wind.speed = speed; }

    [[nodiscard]] const FoliageStatistics& getStats() const noexcept { return m_stats; }
    [[nodiscard]] const std::vector<FoliageInstance>& getInstances() const noexcept { return m_instances; }
    [[nodiscard]] const FoliageInstance* getInstance(uint32_t index) const noexcept {
        return index < m_instances.size() ? &m_instances[index] : nullptr;
    }
    [[nodiscard]] uint32_t getInstanceCount() const noexcept { return static_cast<uint32_t>(m_instances.size()); }
    [[nodiscard]] float getWindTime() const noexcept { return m_windTime; }

private:
    FoliageConfiguration m_config{};
    std::vector<FoliageType> m_types;
    std::unordered_map<uint32_t, size_t> m_typeIndexMap;
    std::vector<FoliageInstance> m_instances;
    mutable FoliageStatistics m_stats{};
    float m_windTime = 0.0f;
    uint32_t m_nextInstanceId = 1;
};

} // namespace sidera
