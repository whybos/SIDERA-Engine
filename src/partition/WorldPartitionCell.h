#pragma once

#include "partition/WorldPartitionGrid.h"
#include "partition/WorldPartitionObject.h"
#include "renderer/BoundingVolume.h"
#include "streaming/StreamingState.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sidera {

class TerrainChunk;
class StreamingRegion;

// An autonomous spatial partition cell representing a discrete tile of the game world
class WorldPartitionCell {
public:
    WorldPartitionCell(uint64_t cellId,
                       const CellCoord& coord,
                       const AABB& worldBounds,
                       float cellSize);
    ~WorldPartitionCell() = default;

    WorldPartitionCell(const WorldPartitionCell&) = delete;
    WorldPartitionCell& operator=(const WorldPartitionCell&) = delete;
    WorldPartitionCell(WorldPartitionCell&&) noexcept = default;
    WorldPartitionCell& operator=(WorldPartitionCell&&) noexcept = default;

    // State & Lifecycle
    [[nodiscard]] uint64_t getId() const noexcept { return m_cellId; }
    [[nodiscard]] const CellCoord& getCoord() const noexcept { return m_coord; }
    [[nodiscard]] const AABB& getBounds() const noexcept { return m_worldBounds; }
    [[nodiscard]] float getCellSize() const noexcept { return m_cellSize; }
    [[nodiscard]] StreamingState getState() const noexcept { return m_state; }
    void setState(StreamingState state) noexcept { m_state = state; }

    [[nodiscard]] bool isLoaded() const noexcept {
        return m_state == StreamingState::Resident || m_state == StreamingState::Active;
    }
    [[nodiscard]] bool isActive() const noexcept {
        return m_state == StreamingState::Active;
    }

    void activate() noexcept;
    void deactivate() noexcept;

    // Hierarchy
    [[nodiscard]] uint64_t getParentMacroCellId() const noexcept { return m_parentMacroCellId; }
    void setParentMacroCellId(uint64_t id) noexcept { m_parentMacroCellId = id; }

    // World Streaming Integration
    [[nodiscard]] uint32_t getLinkedRegionId() const noexcept { return m_linkedRegionId; }
    void setLinkedRegionId(uint32_t regionId) noexcept { m_linkedRegionId = regionId; }
    [[nodiscard]] StreamingRegion* getLinkedRegion() noexcept { return m_linkedRegion; }
    [[nodiscard]] const StreamingRegion* getLinkedRegion() const noexcept { return m_linkedRegion; }
    void setLinkedRegion(StreamingRegion* region) noexcept { m_linkedRegion = region; }

    // Object Ownership & Assignment
    void addOwnedObject(uint32_t objectId);
    bool removeOwnedObject(uint32_t objectId);
    [[nodiscard]] const std::vector<uint32_t>& getOwnedObjects() const noexcept { return m_ownedObjectIds; }
    [[nodiscard]] uint32_t getOwnedObjectCount() const noexcept { return static_cast<uint32_t>(m_ownedObjectIds.size()); }

    void addOverlappingObject(uint32_t objectId);
    bool removeOverlappingObject(uint32_t objectId);
    [[nodiscard]] const std::vector<uint32_t>& getOverlappingObjects() const noexcept { return m_overlappingObjectIds; }

    // Terrain Integration
    void addTerrainChunk(TerrainChunk* chunk);
    bool removeTerrainChunk(TerrainChunk* chunk);
    [[nodiscard]] const std::vector<TerrainChunk*>& getTerrainChunks() const noexcept { return m_terrainChunks; }

    // Foliage Integration
    void addFoliageInstances(const std::vector<uint32_t>& instanceIds);
    void removeFoliageInstances(const std::vector<uint32_t>& instanceIds);
    void clearFoliage() noexcept;
    [[nodiscard]] const std::vector<uint32_t>& getFoliageInstances() const noexcept { return m_foliageInstanceIds; }
    [[nodiscard]] uint32_t getFoliageInstanceCount() const noexcept { return static_cast<uint32_t>(m_foliageInstanceIds.size()); }

    // Distance, Priority, & Frame tracking
    [[nodiscard]] float getDistanceToSource() const noexcept { return m_distanceToNearestSource; }
    void setDistanceToSource(float d) noexcept { m_distanceToNearestSource = d; }

    [[nodiscard]] float getPriority() const noexcept { return m_priority; }
    void setPriority(float p) noexcept { m_priority = p; }

    [[nodiscard]] uint64_t getLastActiveFrame() const noexcept { return m_lastActiveFrame; }
    void setLastActiveFrame(uint64_t frame) noexcept { m_lastActiveFrame = frame; }

    // Memory Tracking
    [[nodiscard]] uint64_t getMemoryUsageBytes() const noexcept;

private:
    uint64_t m_cellId = 0;
    CellCoord m_coord{0, 0};
    AABB m_worldBounds{};
    float m_cellSize = 30.0f;
    StreamingState m_state = StreamingState::Unloaded;

    uint64_t m_parentMacroCellId = 0;
    uint32_t m_linkedRegionId = 0;
    StreamingRegion* m_linkedRegion = nullptr;

    std::vector<uint32_t> m_ownedObjectIds;
    std::vector<uint32_t> m_overlappingObjectIds;
    std::vector<TerrainChunk*> m_terrainChunks;
    std::vector<uint32_t> m_foliageInstanceIds;

    float m_distanceToNearestSource = 0.0f;
    float m_priority = 0.0f;
    uint64_t m_lastActiveFrame = 0;
};

} // namespace sidera
