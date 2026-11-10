#include "lod/LODManager.h"
#include "core/Log.h"

namespace sidera {

LODGroup& LODManager::createLODGroup(std::string_view name) {
    const std::string nameStr(name);
    auto it = m_groups.find(nameStr);
    if (it != m_groups.end()) {
        return *it->second;
    }

    auto group = std::make_unique<LODGroup>(name);
    LODGroup& ref = *group;
    m_groups.emplace(nameStr, std::move(group));
    return ref;
}

LODGroup* LODManager::findLODGroup(std::string_view name) noexcept {
    auto it = m_groups.find(std::string(name));
    return (it != m_groups.end()) ? it->second.get() : nullptr;
}

const LODGroup* LODManager::findLODGroup(std::string_view name) const noexcept {
    auto it = m_groups.find(std::string(name));
    return (it != m_groups.end()) ? it->second.get() : nullptr;
}

bool LODManager::hasLODGroup(std::string_view name) const noexcept {
    return m_groups.contains(std::string(name));
}

void LODManager::toggleSelectionMode() noexcept {
    m_config.selectionMode = (m_config.selectionMode == LODSelectionMode::Distance)
        ? LODSelectionMode::ScreenSpace
        : LODSelectionMode::Distance;
    SIDERA_LOG_INFO("LOD", "Toggled LOD selection mode -> {}", getLODSelectionModeName(m_config.selectionMode));
}

void LODManager::cycleForcedLOD() noexcept {
    switch (m_config.forcedLOD) {
        case ForcedLODMode::Auto:      m_config.forcedLOD = ForcedLODMode::ForceLOD0; break;
        case ForcedLODMode::ForceLOD0: m_config.forcedLOD = ForcedLODMode::ForceLOD1; break;
        case ForcedLODMode::ForceLOD1: m_config.forcedLOD = ForcedLODMode::ForceLOD2; break;
        case ForcedLODMode::ForceLOD2: m_config.forcedLOD = ForcedLODMode::ForceLOD3; break;
        case ForcedLODMode::ForceLOD3: m_config.forcedLOD = ForcedLODMode::Auto;      break;
    }
    SIDERA_LOG_INFO("LOD", "Cycled forced LOD mode -> {}", getForcedLODModeName(m_config.forcedLOD));
}

uint32_t LODManager::selectLOD(const LODGroup& group,
                               float distance,
                               float projectedPixelSize,
                               uint32_t currentLevel) const noexcept {
    return group.selectLevel(distance, projectedPixelSize, currentLevel, m_config);
}

void LODManager::beginFrameStats() noexcept {
    m_stats.totalLODObjects = 0;
    m_stats.visibleLODObjects = 0;
    m_stats.lod0Count = 0;
    m_stats.lod1Count = 0;
    m_stats.lod2Count = 0;
    m_stats.lod3Count = 0;
    m_stats.totalLODTriangles = 0;
    m_stats.originalTriangles = 0;
    m_stats.totalLODGroups = static_cast<uint32_t>(m_groups.size());
}

void LODManager::recordObjectLOD(uint32_t lodLevel, uint64_t originalTriangles, uint64_t currentTriangles) noexcept {
    ++m_stats.totalLODObjects;
    m_stats.originalTriangles += originalTriangles;
    m_stats.totalLODTriangles += currentTriangles;

    switch (lodLevel) {
        case 0: ++m_stats.lod0Count; break;
        case 1: ++m_stats.lod1Count; break;
        case 2: ++m_stats.lod2Count; break;
        default: ++m_stats.lod3Count; break;
    }
}

void LODManager::endFrameStats() noexcept {
    if (m_stats.originalTriangles > 0) {
        const double saved = static_cast<double>(m_stats.originalTriangles - m_stats.totalLODTriangles);
        m_stats.triangleReductionPercent = (saved / static_cast<double>(m_stats.originalTriangles)) * 100.0;
    } else {
        m_stats.triangleReductionPercent = 0.0;
    }
}

void LODManager::clear() noexcept {
    m_groups.clear();
    m_stats = {};
}

} // namespace sidera
