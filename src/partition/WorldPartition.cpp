#include "partition/WorldPartition.h"
#include "terrain/TerrainChunk.h"

#include <algorithm>
#include <unordered_set>

namespace sidera {

WorldPartition::WorldPartition(const WorldPartitionConfig& config)
    : m_config(config) {
    m_config.validate();
}

void WorldPartition::setConfig(const WorldPartitionConfig& config) {
    m_config = config;
    m_config.validate();
}

std::shared_ptr<WorldPartitionCell> WorldPartition::getOrCreateCell(const CellCoord& coord) {
    const uint64_t cellId = WorldPartitionGrid::computeCellId(coord.x, coord.z);
    auto it = m_cells.find(cellId);
    if (it != m_cells.end()) {
        return it->second;
    }

    const AABB bounds = WorldPartitionGrid::cellToWorldBounds(coord, m_config.cellSize, m_config.worldOrigin);
    auto cell = std::make_shared<WorldPartitionCell>(cellId, coord, bounds, m_config.cellSize);

    // Setup macro hierarchy
    const CellCoord macroCoord = WorldPartitionGrid::cellToMacroCoord(coord, m_config.macroCellFactor);
    const uint64_t macroId = WorldPartitionGrid::computeCellId(macroCoord.x, macroCoord.z);
    cell->setParentMacroCellId(macroId);

    m_macroRegions[macroId].push_back(cellId);
    auto macroBoundsIt = m_macroBounds.find(macroId);
    if (macroBoundsIt == m_macroBounds.end()) {
        m_macroBounds[macroId] = bounds;
    } else {
        macroBoundsIt->second.expand(bounds);
    }

    m_cells[cellId] = cell;
    return cell;
}

std::shared_ptr<WorldPartitionCell> WorldPartition::getCell(uint64_t cellId) const {
    auto it = m_cells.find(cellId);
    return it != m_cells.end() ? it->second : nullptr;
}

std::shared_ptr<WorldPartitionCell> WorldPartition::getCell(const CellCoord& coord) const {
    const uint64_t cellId = WorldPartitionGrid::computeCellId(coord.x, coord.z);
    return getCell(cellId);
}

void WorldPartition::clear() {
    m_objects.clear();
    m_cells.clear();
    m_macroRegions.clear();
    m_macroBounds.clear();
    m_nextObjectId = 1;
}

const AABB* WorldPartition::getMacroBounds(uint64_t macroId) const noexcept {
    auto it = m_macroBounds.find(macroId);
    return it != m_macroBounds.end() ? &it->second : nullptr;
}

void WorldPartition::assignObjectToCells(WorldPartitionObject& obj) {
    // 1. Determine canonical owner cell based on object origin position
    const CellCoord ownerCoord = WorldPartitionGrid::worldToCell(obj.position, m_config.cellSize, m_config.worldOrigin);
    auto ownerCell = getOrCreateCell(ownerCoord);
    obj.ownerCellId = ownerCell->getId();
    ownerCell->addOwnedObject(obj.id);

    // 2. Determine overlapping cells whose boundaries intersect the object's world bounds
    obj.overlappingCellIds.clear();
    CellCoord minCoord{}, maxCoord{};
    WorldPartitionGrid::getIntersectingCellRange(obj.worldBounds, m_config.cellSize, m_config.worldOrigin, minCoord, maxCoord);

    for (int32_t x = minCoord.x; x <= maxCoord.x; ++x) {
        for (int32_t z = minCoord.z; z <= maxCoord.z; ++z) {
            if (x == ownerCoord.x && z == ownerCoord.z) {
                continue; // Skip canonical owner
            }
            auto cell = getOrCreateCell(CellCoord{x, z});
            cell->addOverlappingObject(obj.id);
            obj.overlappingCellIds.push_back(cell->getId());
        }
    }
}

void WorldPartition::removeObjectFromAllCells(const WorldPartitionObject& obj) {
    if (auto ownerCell = getCell(obj.ownerCellId)) {
        ownerCell->removeOwnedObject(obj.id);
    }
    for (const uint64_t cellId : obj.overlappingCellIds) {
        if (auto cell = getCell(cellId)) {
            cell->removeOverlappingObject(obj.id);
        }
    }
}

uint32_t WorldPartition::registerObject(const std::string& name,
                                       PartitionObjectType type,
                                       const glm::vec3& position,
                                       const AABB& bounds,
                                       void* userPointer,
                                       uint64_t memoryBytes) {
    const uint32_t id = m_nextObjectId++;
    WorldPartitionObject obj{
        .id = id,
        .name = name,
        .type = type,
        .position = position,
        .worldBounds = bounds,
        .ownerCellId = 0,
        .userPointer = userPointer,
        .memoryBytes = memoryBytes,
        .active = true
    };

    assignObjectToCells(obj);
    m_objects[id] = std::move(obj);
    return id;
}

bool WorldPartition::unregisterObject(uint32_t objectId) {
    auto it = m_objects.find(objectId);
    if (it == m_objects.end()) {
        return false;
    }

    removeObjectFromAllCells(it->second);
    m_objects.erase(it);
    return true;
}

bool WorldPartition::moveObject(uint32_t objectId, const glm::vec3& newPosition, const AABB& newBounds) {
    auto it = m_objects.find(objectId);
    if (it == m_objects.end()) {
        return false;
    }

    WorldPartitionObject& obj = it->second;
    removeObjectFromAllCells(obj);

    obj.position = newPosition;
    obj.worldBounds = newBounds;
    assignObjectToCells(obj);
    return true;
}

const WorldPartitionObject* WorldPartition::getObject(uint32_t objectId) const {
    auto it = m_objects.find(objectId);
    return it != m_objects.end() ? &it->second : nullptr;
}

WorldPartitionObject* WorldPartition::getObject(uint32_t objectId) {
    auto it = m_objects.find(objectId);
    return it != m_objects.end() ? &it->second : nullptr;
}

bool WorldPartition::registerTerrainChunk(TerrainChunk* chunk) {
    if (!chunk) return false;

    // Use chunk center to determine canonical cell
    const glm::vec3 center = chunk->getBounds().getCenter();
    const CellCoord coord = WorldPartitionGrid::worldToCell(center, m_config.cellSize, m_config.worldOrigin);
    auto cell = getOrCreateCell(coord);
    cell->addTerrainChunk(chunk);
    return true;
}

bool WorldPartition::unregisterTerrainChunk(uint32_t chunkId) {
    bool removed = false;
    for (auto& [id, cell] : m_cells) {
        if (!cell) continue;
        for (auto* chunk : cell->getTerrainChunks()) {
            if (chunk && chunk->getId() == chunkId) {
                cell->removeTerrainChunk(chunk);
                removed = true;
                break;
            }
        }
    }
    return removed;
}

bool WorldPartition::registerFoliageToCell(uint64_t cellId, const std::vector<uint32_t>& instanceIds) {
    auto cell = getCell(cellId);
    if (!cell) return false;
    cell->addFoliageInstances(instanceIds);
    return true;
}

bool WorldPartition::removeFoliageFromCell(uint64_t cellId, const std::vector<uint32_t>& instanceIds) {
    auto cell = getCell(cellId);
    if (!cell) return false;
    cell->removeFoliageInstances(instanceIds);
    return true;
}

std::shared_ptr<WorldPartitionCell> WorldPartition::getCellAtPosition(const glm::vec3& worldPos) const {
    const CellCoord coord = WorldPartitionGrid::worldToCell(worldPos, m_config.cellSize, m_config.worldOrigin);
    return getCell(coord);
}

std::vector<std::shared_ptr<WorldPartitionCell>> WorldPartition::getCellsInRadius(const glm::vec3& worldPos, float radius) const {
    std::vector<std::shared_ptr<WorldPartitionCell>> results;
    if (radius <= 0.0f) return results;

    const AABB searchBox{
        .min = worldPos - glm::vec3(radius),
        .max = worldPos + glm::vec3(radius)
    };

    CellCoord minCoord{}, maxCoord{};
    WorldPartitionGrid::getIntersectingCellRange(searchBox, m_config.cellSize, m_config.worldOrigin, minCoord, maxCoord);

    const float radiusSq = radius * radius;
    for (int32_t x = minCoord.x; x <= maxCoord.x; ++x) {
        for (int32_t z = minCoord.z; z <= maxCoord.z; ++z) {
            auto cell = getCell(CellCoord{x, z});
            if (!cell) continue;

            const AABB& b = cell->getBounds();
            const glm::vec3 closestPoint = glm::clamp(worldPos, b.min, b.max);
            const glm::vec3 diff = worldPos - closestPoint;
            const float distSq = glm::dot(diff, diff);

            if (distSq <= radiusSq) {
                results.push_back(cell);
            }
        }
    }
    return results;
}

std::vector<std::shared_ptr<WorldPartitionCell>> WorldPartition::getCellsInBounds(const AABB& aabb) const {
    std::vector<std::shared_ptr<WorldPartitionCell>> results;
    if (!aabb.isValid()) return results;

    CellCoord minCoord{}, maxCoord{};
    WorldPartitionGrid::getIntersectingCellRange(aabb, m_config.cellSize, m_config.worldOrigin, minCoord, maxCoord);

    for (int32_t x = minCoord.x; x <= maxCoord.x; ++x) {
        for (int32_t z = minCoord.z; z <= maxCoord.z; ++z) {
            auto cell = getCell(CellCoord{x, z});
            if (cell && cell->getBounds().intersects(aabb)) {
                results.push_back(cell);
            }
        }
    }
    return results;
}

std::vector<std::shared_ptr<WorldPartitionCell>> WorldPartition::getNeighborCells(const CellCoord& coord, int32_t radius) const {
    std::vector<std::shared_ptr<WorldPartitionCell>> results;
    const int32_t r = std::max(1, radius);
    for (int32_t dx = -r; dx <= r; ++dx) {
        for (int32_t dz = -r; dz <= r; ++dz) {
            if (dx == 0 && dz == 0) continue;
            auto cell = getCell(CellCoord{coord.x + dx, coord.z + dz});
            if (cell) {
                results.push_back(cell);
            }
        }
    }
    return results;
}

std::vector<const WorldPartitionObject*> WorldPartition::getObjectsInBounds(const AABB& aabb) const {
    std::vector<const WorldPartitionObject*> results;
    if (!aabb.isValid()) return results;

    std::unordered_set<uint32_t> visited;
    const auto cells = getCellsInBounds(aabb);

    for (const auto& cell : cells) {
        if (!cell) continue;

        // Check canonical owned objects
        for (const uint32_t id : cell->getOwnedObjects()) {
            if (!visited.insert(id).second) continue;
            auto it = m_objects.find(id);
            if (it != m_objects.end() && it->second.worldBounds.intersects(aabb)) {
                results.push_back(&it->second);
            }
        }

        // Check overlapping boundary objects
        for (const uint32_t id : cell->getOverlappingObjects()) {
            if (!visited.insert(id).second) continue;
            auto it = m_objects.find(id);
            if (it != m_objects.end() && it->second.worldBounds.intersects(aabb)) {
                results.push_back(&it->second);
            }
        }
    }
    return results;
}

std::vector<TerrainChunk*> WorldPartition::getTerrainInBounds(const AABB& aabb) const {
    std::vector<TerrainChunk*> results;
    if (!aabb.isValid()) return results;

    std::unordered_set<TerrainChunk*> visited;
    const auto cells = getCellsInBounds(aabb);

    for (const auto& cell : cells) {
        if (!cell) continue;
        for (auto* chunk : cell->getTerrainChunks()) {
            if (!chunk) continue;
            if (visited.insert(chunk).second && chunk->getBounds().intersects(aabb)) {
                results.push_back(chunk);
            }
        }
    }
    return results;
}

std::vector<uint32_t> WorldPartition::getFoliageInBounds(const AABB& aabb) const {
    std::vector<uint32_t> results;
    if (!aabb.isValid()) return results;

    const auto cells = getCellsInBounds(aabb);
    for (const auto& cell : cells) {
        if (!cell) continue;
        const auto& instances = cell->getFoliageInstances();
        results.insert(results.end(), instances.begin(), instances.end());
    }
    return results;
}

} // namespace sidera
