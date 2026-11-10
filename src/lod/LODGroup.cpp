#include "lod/LODGroup.h"
#include "renderer/Mesh.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sidera {

LODGroup::LODGroup(std::string_view name)
    : m_name(name) {
}

void LODGroup::addLevel(const Mesh* mesh,
                        float maxDistance,
                        float minScreenSize,
                        float qualityReduction) {
    LODLevel level{};
    level.mesh = mesh;
    level.maxDistance = maxDistance;
    level.minScreenSize = minScreenSize;
    level.qualityReduction = qualityReduction;

    if (mesh) {
        level.triangleCount = mesh->getIndexCount() / 3;
        level.vertexCount = mesh->getVertexCount();
    }

    m_levels.push_back(level);
}

void LODGroup::clear() noexcept {
    m_levels.clear();
}

const LODLevel& LODGroup::getLevel(uint32_t index) const {
    if (index >= m_levels.size()) {
        static const LODLevel s_empty{};
        return s_empty;
    }
    return m_levels[index];
}

void LODGroup::setDistanceThresholds(float lod0Max, float lod1Max, float lod2Max, float lod3Max) {
    const float thresholds[4] = {lod0Max, lod1Max, lod2Max, lod3Max};
    for (size_t i = 0; i < m_levels.size() && i < 4; ++i) {
        m_levels[i].maxDistance = thresholds[i];
    }
}

void LODGroup::setScreenSizeThresholds(float lod0Min, float lod1Min, float lod2Min, float lod3Min) {
    const float thresholds[4] = {lod0Min, lod1Min, lod2Min, lod3Min};
    for (size_t i = 0; i < m_levels.size() && i < 4; ++i) {
        m_levels[i].minScreenSize = thresholds[i];
    }
}

uint32_t LODGroup::selectLevel(float distance,
                               float projectedPixelSize,
                               uint32_t currentLevel,
                               const LODConfig& config) const noexcept {
    if (m_levels.empty()) {
        return 0;
    }

    const uint32_t maxIdx = static_cast<uint32_t>(m_levels.size() - 1);

    // 1. Check if LOD system is disabled
    if (!config.enabled) {
        return 0;
    }

    // 2. Check for explicit manual override (Force LOD 0..3)
    if (config.forcedLOD != ForcedLODMode::Auto) {
        const int32_t forced = static_cast<int32_t>(config.forcedLOD);
        return std::clamp(static_cast<uint32_t>(std::max(0, forced)), 0u, maxIdx);
    }

    // Single-level groups always return 0
    if (m_levels.size() == 1) {
        return 0;
    }

    currentLevel = std::min(currentLevel, maxIdx);

    // 3. Selection mode: Distance-based evaluation with hysteresis
    if (config.selectionMode == LODSelectionMode::Distance) {
        const float H = std::max(0.0f, config.distanceHysteresis);

        // Find baseline level without hysteresis first
        uint32_t targetLevel = maxIdx;
        for (uint32_t i = 0; i < maxIdx; ++i) {
            // Apply deadband hysteresis depending on current state:
            // If already at currentLevel, require extra distance (H) to step down to lower detail (i+1),
            // and require closer approach (-H) to step up to higher detail (i).
            float threshold = m_levels[i].maxDistance;
            if (i == currentLevel) {
                threshold += H; // Stickier in current tier when moving away
            } else if (i + 1 == currentLevel) {
                threshold -= H; // Stickier in current tier when approaching
            }

            if (distance <= threshold) {
                targetLevel = i;
                break;
            }
        }
        return targetLevel;
    }

    // 4. Selection mode: Screen-space pixel coverage with hysteresis
    if (config.selectionMode == LODSelectionMode::ScreenSpace) {
        const float H = std::max(0.0f, config.screenHysteresis);

        // In screen space, higher pixel size = higher detail (LOD 0)
        uint32_t targetLevel = maxIdx;
        for (uint32_t i = 0; i < maxIdx; ++i) {
            float threshold = m_levels[i].minScreenSize;
            if (i == currentLevel) {
                threshold -= H; // Stickier in current tier when shrinking
            } else if (i + 1 == currentLevel) {
                threshold += H; // Stickier in current tier when growing
            }

            if (projectedPixelSize >= threshold) {
                targetLevel = i;
                break;
            }
        }
        return targetLevel;
    }

    return 0;
}

} // namespace sidera
