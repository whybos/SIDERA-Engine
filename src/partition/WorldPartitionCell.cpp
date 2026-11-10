#include "partition/WorldPartitionCell.h"
#include "streaming/StreamingRegion.h"
#include "terrain/TerrainChunk.h"

#include <algorithm>
#include <unordered_set>

namespace sidera {

WorldPartitionCell::WorldPartitionCell(uint64_t cellId,
                                       const CellCoord& coord,
                                       const AABB& worldBounds,
                                       float cellSize)
    : m_cellId(cellId)
    , m_coord(coord)
    , m_worldBounds(worldBounds)
    , m_cellSize(cellSize)
    , m_state(StreamingState::Unloaded) {}

void WorldPartitionCell::activate() noexcept {
    m_state = StreamingState::Active;
}

void WorldPartitionCell::deactivate() noexcept {
    if (m_state == StreamingState::Active) {
        m_state = StreamingState::Resident;
    }
}

void WorldPartitionCell::addOwnedObject(uint32_t objectId) {
    if (std::find(m_ownedObjectIds.begin(), m_ownedObjectIds.end(), objectId) == m_ownedObjectIds.end()) {
        m_ownedObjectIds.push_back(objectId);
    }
}

bool WorldPartitionCell::removeOwnedObject(uint32_t objectId) {
    auto it = std::find(m_ownedObjectIds.begin(), m_ownedObjectIds.end(), objectId);
    if (it != m_ownedObjectIds.end()) {
        m_ownedObjectIds.erase(it);
        return true;
    }
    return false;
}

void WorldPartitionCell::addOverlappingObject(uint32_t objectId) {
    if (std::find(m_overlappingObjectIds.begin(), m_overlappingObjectIds.end(), objectId) == m_overlappingObjectIds.end()) {
        m_overlappingObjectIds.push_back(objectId);
    }
}

bool WorldPartitionCell::removeOverlappingObject(uint32_t objectId) {
    auto it = std::find(m_overlappingObjectIds.begin(), m_overlappingObjectIds.end(), objectId);
    if (it != m_overlappingObjectIds.end()) {
        m_overlappingObjectIds.erase(it);
        return true;
    }
    return false;
}

void WorldPartitionCell::addTerrainChunk(TerrainChunk* chunk) {
    if (!chunk) return;
    if (std::find(m_terrainChunks.begin(), m_terrainChunks.end(), chunk) == m_terrainChunks.end()) {
        m_terrainChunks.push_back(chunk);
    }
}

bool WorldPartitionCell::removeTerrainChunk(TerrainChunk* chunk) {
    auto it = std::find(m_terrainChunks.begin(), m_terrainChunks.end(), chunk);
    if (it != m_terrainChunks.end()) {
        m_terrainChunks.erase(it);
        return true;
    }
    return false;
}

void WorldPartitionCell::addFoliageInstances(const std::vector<uint32_t>& instanceIds) {
    m_foliageInstanceIds.reserve(m_foliageInstanceIds.size() + instanceIds.size());
    for (const uint32_t id : instanceIds) {
        m_foliageInstanceIds.push_back(id);
    }
}

void WorldPartitionCell::removeFoliageInstances(const std::vector<uint32_t>& instanceIds) {
    if (instanceIds.empty() || m_foliageInstanceIds.empty()) return;
    std::unordered_set<uint32_t> toRemove(instanceIds.begin(), instanceIds.end());
    m_foliageInstanceIds.erase(
        std::remove_if(m_foliageInstanceIds.begin(), m_foliageInstanceIds.end(),
                       [&toRemove](uint32_t id) { return toRemove.contains(id); }),
        m_foliageInstanceIds.end()
    );
}

void WorldPartitionCell::clearFoliage() noexcept {
    m_foliageInstanceIds.clear();
}

uint64_t WorldPartitionCell::getMemoryUsageBytes() const noexcept {
    uint64_t bytes = 0;
    if (m_linkedRegion) {
        bytes += m_linkedRegion->getGpuMemoryBytes() + m_linkedRegion->getCpuMemoryBytes();
    } else {
        for (const auto* chunk : m_terrainChunks) {
            if (chunk && chunk->getMesh()) {
                bytes += chunk->getMesh()->getGpuMemoryUsageBytes();
            }
        }
        // Approximate foliage instance memory: ~64 bytes per instance
        bytes += static_cast<uint64_t>(m_foliageInstanceIds.size()) * 64ull;
    }
    return bytes;
}

} // namespace sidera
