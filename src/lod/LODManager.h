#pragma once

#include "lod/LODConfig.h"
#include "lod/LODGroup.h"
#include "lod/LODStats.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sidera {

class LODManager {
public:
    LODManager() = default;
    ~LODManager() = default;

    LODManager(const LODManager&) = delete;
    LODManager& operator=(const LODManager&) = delete;
    LODManager(LODManager&&) noexcept = default;
    LODManager& operator=(LODManager&&) noexcept = default;

    // Registers a new LODGroup under a unique identifier
    LODGroup& createLODGroup(std::string_view name);

    // Lookups
    [[nodiscard]] LODGroup* findLODGroup(std::string_view name) noexcept;
    [[nodiscard]] const LODGroup* findLODGroup(std::string_view name) const noexcept;
    [[nodiscard]] bool hasLODGroup(std::string_view name) const noexcept;

    // Configuration accessors
    [[nodiscard]] const LODConfig& getConfig() const noexcept { return m_config; }
    [[nodiscard]] LODConfig& getConfig() noexcept { return m_config; }
    void setConfig(const LODConfig& config) noexcept { m_config = config; }

    // Quick runtime adjustments
    void setSelectionMode(LODSelectionMode mode) noexcept { m_config.selectionMode = mode; }
    void toggleSelectionMode() noexcept;
    void setForcedLOD(ForcedLODMode mode) noexcept { m_config.forcedLOD = mode; }
    void cycleForcedLOD() noexcept;
    void setEnabled(bool enabled) noexcept { m_config.enabled = enabled; }
    void setDistanceHysteresis(float hysteresis) noexcept { m_config.distanceHysteresis = hysteresis; }
    void setScreenHysteresis(float hysteresis) noexcept { m_config.screenHysteresis = hysteresis; }
    void setShadowLodBias(int32_t bias) noexcept { m_config.shadowLodBias = bias; }

    // Evaluates LOD level for an object given its parameters
    [[nodiscard]] uint32_t selectLOD(
        const LODGroup& group,
        float distance,
        float projectedPixelSize,
        uint32_t currentLevel
    ) const noexcept;

    // Statistics accounting
    void beginFrameStats() noexcept;
    void recordObjectLOD(uint32_t lodLevel, uint64_t originalTriangles, uint64_t currentTriangles) noexcept;
    void recordSelectionTime(float timeMs) noexcept { m_stats.selectionTimeMs = timeMs; }
    void endFrameStats() noexcept;

    [[nodiscard]] const LODStatistics& getStats() const noexcept { return m_stats; }
    [[nodiscard]] size_t getGroupCount() const noexcept { return m_groups.size(); }

    void clear() noexcept;

private:
    LODConfig m_config{};
    LODStatistics m_stats{};
    std::unordered_map<std::string, std::unique_ptr<LODGroup>> m_groups;
};

} // namespace sidera
