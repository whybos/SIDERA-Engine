#pragma once

#include "lod/LODConfig.h"
#include "lod/LODLevel.h"

#include <string>
#include <string_view>
#include <vector>

namespace sidera {

class Mesh;

class LODGroup {
public:
    explicit LODGroup(std::string_view name);
    ~LODGroup() = default;

    LODGroup(const LODGroup&) = delete;
    LODGroup& operator=(const LODGroup&) = delete;
    LODGroup(LODGroup&&) noexcept = default;
    LODGroup& operator=(LODGroup&&) noexcept = default;

    // Adds a detail tier (ordered from highest detail index 0 to lowest detail)
    void addLevel(const Mesh* mesh,
                  float maxDistance,
                  float minScreenSize = 0.0f,
                  float qualityReduction = 1.0f);

    // Clears all configured levels
    void clear() noexcept;

    // Evaluates optimal LOD index considering distance/screen metrics, hysteresis, and overrides
    [[nodiscard]] uint32_t selectLevel(
        float distance,
        float projectedPixelSize,
        uint32_t currentLevel,
        const LODConfig& config
    ) const noexcept;

    // Configures standard distance thresholds across up to 4 levels
    void setDistanceThresholds(float lod0Max, float lod1Max, float lod2Max, float lod3Max = 1e6f);

    // Configures standard screen-space pixel thresholds across up to 4 levels
    void setScreenSizeThresholds(float lod0Min, float lod1Min, float lod2Min, float lod3Min = 0.0f);

    [[nodiscard]] uint32_t getLevelCount() const noexcept { return static_cast<uint32_t>(m_levels.size()); }
    [[nodiscard]] const LODLevel& getLevel(uint32_t index) const;
    [[nodiscard]] const std::vector<LODLevel>& getLevels() const noexcept { return m_levels; }
    [[nodiscard]] std::string_view getName() const noexcept { return m_name; }

private:
    std::string m_name;
    std::vector<LODLevel> m_levels;
};

} // namespace sidera
