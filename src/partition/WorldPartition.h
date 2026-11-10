#pragma once

#include "partition/WorldPartitionCell.h"
#include "partition/WorldPartitionConfig.h"
#include "partition/WorldPartitionGrid.h"
#include "partition/WorldPartitionObject.h"
#include "renderer/BoundingVolume.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera {

class TerrainChunk;

// Spatial indexing database managing partition cells, canonical object ownership, and cell hierarchy
class WorldPartition {
public:
    explicit WorldPartition(const WorldPartitionConfig& config = {});
    ~WorldPartition() = default;

    WorldPartition(const WorldPartition&) = delete;
    WorldPartition& operator=(const WorldPartition&) = delete;
    WorldPartition(WorldPartition&&) noexcept = default;
    WorldPartition& operator=(WorldPartition&&) noexcept = default;

    // Configuration
    [[nodiscard]] const WorldPartitionConfig& getConfig() const noexcept { return m_config; }
    void setConfig(const WorldPartitionConfig& config);

    // Cell Management
    std::shared_ptr<WorldPartitionCell> getOrCreateCell(const CellCoord& coord);
    [[nodiscard]] std::shared_ptr<WorldPartitionCell> getCell(uint64_t cellId) const;
    [[nodiscard]] std::shared_ptr<WorldPartitionCell> getCell(const CellCoord& coord) const;
    [[nodiscard]] const std::unordered_map<uint64_t, std::shared_ptr<WorldPartitionCell>>& getAllCells() const noexcept {
        return m_cells;
    }
    [[nodiscard]] size_t getCellCount() const noexcept { return m_cells.size(); }
    void clear();

    // Object Registration & Assignment
    uint32_t registerObject(const std::string& name,
                            PartitionObjectType type,
                            const glm::vec3& position,
                            const AABB& bounds,
                            void* userPointer = nullptr,
                            uint64_t memoryBytes = 0);
    bool unregisterObject(uint32_t objectId);
    bool moveObject(uint32_t objectId, const glm::vec3& newPosition, const AABB& newBounds);

    [[nodiscard]] const WorldPartitionObject* getObject(uint32_t objectId) const;
    [[nodiscard]] WorldPartitionObject* getObject(uint32_t objectId);
    [[nodiscard]] const std::unordered_map<uint32_t, WorldPartitionObject>& getAllObjects() const noexcept {
        return m_objects;
    }
    [[nodiscard]] size_t getObjectCount() const noexcept { return m_objects.size(); }

    // Terrain Integration
    bool registerTerrainChunk(TerrainChunk* chunk);
    bool unregisterTerrainChunk(uint32_t chunkId);

    // Foliage Integration
    bool registerFoliageToCell(uint64_t cellId, const std::vector<uint32_t>& instanceIds);
    bool removeFoliageFromCell(uint64_t cellId, const std::vector<uint32_t>& instanceIds);

    // Hierarchy (Macro Regions)
    [[nodiscard]] const std::unordered_map<uint64_t, std::vector<uint64_t>>& getMacroRegions() const noexcept {
        return m_macroRegions;
    }
    [[nodiscard]] const AABB* getMacroBounds(uint64_t macroId) const noexcept;

    // Spatial Queries
    [[nodiscard]] std::shared_ptr<WorldPartitionCell> getCellAtPosition(const glm::vec3& worldPos) const;
    [[nodiscard]] std::vector<std::shared_ptr<WorldPartitionCell>> getCellsInRadius(const glm::vec3& worldPos, float radius) const;
    [[nodiscard]] std::vector<std::shared_ptr<WorldPartitionCell>> getCellsInBounds(const AABB& aabb) const;
    [[nodiscard]] std::vector<std::shared_ptr<WorldPartitionCell>> getNeighborCells(const CellCoord& coord, int32_t radius = 1) const;

    [[nodiscard]] std::vector<const WorldPartitionObject*> getObjectsInBounds(const AABB& aabb) const;
    [[nodiscard]] std::vector<TerrainChunk*> getTerrainInBounds(const AABB& aabb) const;
    [[nodiscard]] std::vector<uint32_t> getFoliageInBounds(const AABB& aabb) const;

private:
    void assignObjectToCells(WorldPartitionObject& obj);
    void removeObjectFromAllCells(const WorldPartitionObject& obj);

    WorldPartitionConfig m_config{};
    std::unordered_map<uint64_t, std::shared_ptr<WorldPartitionCell>> m_cells;
    std::unordered_map<uint64_t, std::vector<uint64_t>> m_macroRegions;
    std::unordered_map<uint64_t, AABB> m_macroBounds;
    std::unordered_map<uint32_t, WorldPartitionObject> m_objects;
    uint32_t m_nextObjectId = 1;
};

} // namespace sidera
