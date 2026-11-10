#include "partition/WorldPartitionManager.h"
#include "core/Log.h"
#include "foliage/FoliageSystem.h"
#include "streaming/WorldStreamingManager.h"
#include "terrain/Terrain.h"

#include <chrono>
#include <limits>

namespace sidera {

WorldPartitionManager::WorldPartitionManager() = default;

WorldPartitionManager::~WorldPartitionManager() = default;

bool WorldPartitionManager::init(const WorldPartitionConfig& config,
                                 WorldStreamingManager* streamingManager,
                                 Terrain* terrain,
                                 FoliageSystem* foliageSystem) {
    m_config = config;
    m_config.validate();
    m_partition.setConfig(m_config);

    m_streamingManager = streamingManager;
    m_terrain = terrain;
    m_foliageSystem = foliageSystem;

    // Register primary camera observer source
    StreamingSource primarySource{
        .id = 1,
        .name = "PrimaryCamera",
        .enabled = true,
        .position = glm::vec3(0.0f),
        .direction = glm::vec3(0.0f, 0.0f, -1.0f),
        .velocity = glm::vec3(0.0f),
        .radiusMultiplier = 1.0f,
        .priorityWeight = 1.0f
    };
    registerSource(primarySource);

    synchronizeWithStreaming();

    m_initialized = true;
    SIDERA_LOG_INFO("WorldPartition",
                    "World Partition initialized (cellSize: {:.1f}m, cells: {}, objects: {}).",
                    m_config.cellSize, m_partition.getCellCount(), m_partition.getObjectCount());
    return true;
}

void WorldPartitionManager::synchronizeWithStreaming() {
    if (!m_streamingManager) return;

    for (const auto& reg : m_streamingManager->getRegions()) {
        if (!reg) continue;

        // Map region (gridX, gridZ) to CellCoord
        const CellCoord coord{static_cast<int32_t>(reg->getGridX()), static_cast<int32_t>(reg->getGridZ())};
        auto cell = m_partition.getOrCreateCell(coord);
        cell->setLinkedRegionId(reg->getId());
        cell->setLinkedRegion(reg.get());
        cell->setState(reg->getState());

        if (reg->getTerrainChunk()) {
            cell->addTerrainChunk(reg->getTerrainChunk());
        }
    }
}

uint32_t WorldPartitionManager::registerSource(const StreamingSource& source) {
    for (auto& s : m_sources) {
        if (s.id == source.id) {
            s = source;
            return s.id;
        }
    }
    m_sources.push_back(source);
    return source.id;
}

void WorldPartitionManager::unregisterSource(uint32_t sourceId) {
    std::erase_if(m_sources, [sourceId](const StreamingSource& s) { return s.id == sourceId; });
}

void WorldPartitionManager::updateSource(uint32_t sourceId,
                                         const glm::vec3& position,
                                         const glm::vec3& direction,
                                         const glm::vec3& velocity) {
    for (auto& s : m_sources) {
        if (s.id == sourceId) {
            s.position = position;
            s.direction = direction;
            s.velocity = velocity;
            return;
        }
    }
}

uint32_t WorldPartitionManager::registerObject(const std::string& name,
                                               PartitionObjectType type,
                                               const glm::vec3& position,
                                               const AABB& bounds,
                                               void* userPointer,
                                               uint64_t memoryBytes) {
    return m_partition.registerObject(name, type, position, bounds, userPointer, memoryBytes);
}

bool WorldPartitionManager::unregisterObject(uint32_t objectId) {
    return m_partition.unregisterObject(objectId);
}

bool WorldPartitionManager::moveObject(uint32_t objectId, const glm::vec3& newPosition, const AABB& newBounds) {
    return m_partition.moveObject(objectId, newPosition, newBounds);
}

const WorldPartitionObject* WorldPartitionManager::getObject(uint32_t objectId) const {
    return m_partition.getObject(objectId);
}

bool WorldPartitionManager::registerTerrainChunk(TerrainChunk* chunk) {
    return m_partition.registerTerrainChunk(chunk);
}

bool WorldPartitionManager::unregisterTerrainChunk(uint32_t chunkId) {
    return m_partition.unregisterTerrainChunk(chunkId);
}

bool WorldPartitionManager::registerFoliageToCell(uint64_t cellId, const std::vector<uint32_t>& instanceIds) {
    return m_partition.registerFoliageToCell(cellId, instanceIds);
}

std::shared_ptr<WorldPartitionCell> WorldPartitionManager::getCellAtPosition(const glm::vec3& worldPos) {
    const auto start = std::chrono::high_resolution_clock::now();
    auto cell = m_partition.getCellAtPosition(worldPos);
    const auto end = std::chrono::high_resolution_clock::now();
    m_stats.cellLookupTimeUs = std::chrono::duration<float, std::micro>(end - start).count();
    return cell;
}

std::vector<std::shared_ptr<WorldPartitionCell>> WorldPartitionManager::getCellsInRadius(const glm::vec3& worldPos, float radius) {
    const auto start = std::chrono::high_resolution_clock::now();
    auto cells = m_partition.getCellsInRadius(worldPos, radius);
    const auto end = std::chrono::high_resolution_clock::now();
    m_stats.spatialQueryTimeUs = std::chrono::duration<float, std::micro>(end - start).count();
    return cells;
}

std::vector<std::shared_ptr<WorldPartitionCell>> WorldPartitionManager::getCellsInBounds(const AABB& aabb) {
    const auto start = std::chrono::high_resolution_clock::now();
    auto cells = m_partition.getCellsInBounds(aabb);
    const auto end = std::chrono::high_resolution_clock::now();
    m_stats.spatialQueryTimeUs = std::chrono::duration<float, std::micro>(end - start).count();
    return cells;
}

std::vector<std::shared_ptr<WorldPartitionCell>> WorldPartitionManager::getNeighborCells(const CellCoord& coord, int32_t radius) {
    return m_partition.getNeighborCells(coord, radius);
}

std::vector<const WorldPartitionObject*> WorldPartitionManager::getObjectsInBounds(const AABB& aabb) {
    const auto start = std::chrono::high_resolution_clock::now();
    auto objects = m_partition.getObjectsInBounds(aabb);
    const auto end = std::chrono::high_resolution_clock::now();
    m_stats.spatialQueryTimeUs = std::chrono::duration<float, std::micro>(end - start).count();
    return objects;
}

std::vector<TerrainChunk*> WorldPartitionManager::getTerrainInBounds(const AABB& aabb) {
    return m_partition.getTerrainInBounds(aabb);
}

std::vector<uint32_t> WorldPartitionManager::getFoliageInBounds(const AABB& aabb) {
    return m_partition.getFoliageInBounds(aabb);
}

void WorldPartitionManager::setConfig(const WorldPartitionConfig& config) {
    m_config = config;
    m_config.validate();
    m_partition.setConfig(m_config);
}

void WorldPartitionManager::update(float deltaTime, const Camera& camera) {
    if (!m_initialized || !m_config.enabled) {
        return;
    }

    ++m_frameIndex;

    // 1. Update primary streaming source
    const glm::vec3 camPos = camera.getPosition();
    const float distMoved = glm::length(camPos - m_lastCameraPos);
    glm::vec3 vel{0.0f};
    if (deltaTime > 1e-4f && distMoved > 1e-4f && distMoved < 20.0f) {
        vel = (camPos - m_lastCameraPos) / deltaTime;
        const float maxSpeed = 50.0f;
        if (glm::length(vel) > maxSpeed) {
            vel = glm::normalize(vel) * maxSpeed;
        }
    }
    m_lastCameraPos = camPos;
    updateSource(1, camPos, camera.getForward(), vel);

    // 2. Evaluate distance-based hysteresis & view frustum priority
    const glm::mat4 vp = camera.getProjectionMatrix() * camera.getViewMatrix();
    const Frustum frustum = Frustum::fromViewProj(vp);
    evaluateHysteresisAndStreaming(camera, frustum);

    // 3. Enforce cell and memory limits
    enforceCellBudget();

    // 4. Update diagnostic stats
    updateStats();
}

void WorldPartitionManager::evaluateHysteresisAndStreaming(const Camera& camera, const Frustum& frustum) {
    (void)camera;

    for (const auto& [cellId, cell] : m_partition.getAllCells()) {
        if (!cell) continue;

        // Compute minimum distance to any active streaming source
        float minDist = std::numeric_limits<float>::max();
        for (const auto& src : m_sources) {
            if (!src.enabled) continue;

            const glm::vec3 predPos = src.getPredictedPosition(1.0f);
            const AABB& bounds = cell->getBounds();
            const glm::vec3 closestPoint = glm::clamp(predPos, bounds.min, bounds.max);
            const float dist = glm::length(predPos - closestPoint);
            const float scaledDist = dist / std::max(0.01f, src.radiusMultiplier);

            minDist = std::min(minDist, scaledDist);
        }

        cell->setDistanceToSource(minDist);

        // Priority calculation
        const bool inFrustum = frustum.intersectsAABB(cell->getBounds());
        float priority = std::max(0.0f, 2000.0f - minDist);
        if (inFrustum) {
            priority += 50.0f;
        }
        cell->setPriority(priority);

        // State Machine transitions with distance hysteresis
        StreamingRegion* linkedRegion = cell->getLinkedRegion();
        if (linkedRegion) {
            // Mirror underlying StreamingRegion state
            cell->setState(linkedRegion->getState());

            if (linkedRegion->getState() == StreamingState::Resident && minDist <= m_config.loadRadius) {
                cell->activate();
                cell->setLastActiveFrame(m_frameIndex);
            } else if (linkedRegion->getState() == StreamingState::Active) {
                cell->setLastActiveFrame(m_frameIndex);
                if (minDist > m_config.unloadRadius) {
                    cell->deactivate();
                }
            }
        } else {
            // Standalone partition cells (e.g., custom meshes or unit testing)
            const StreamingState curState = cell->getState();
            if (curState == StreamingState::Unloaded) {
                if (minDist <= m_config.loadRadius) {
                    cell->setState(StreamingState::Resident);
                }
            } else if (curState == StreamingState::Resident) {
                if (minDist <= m_config.loadRadius) {
                    cell->activate();
                    cell->setLastActiveFrame(m_frameIndex);
                } else if (minDist > m_config.unloadRadius) {
                    cell->setState(StreamingState::Unloaded);
                }
            } else if (curState == StreamingState::Active) {
                cell->setLastActiveFrame(m_frameIndex);
                if (minDist > m_config.unloadRadius) {
                    cell->deactivate();
                    cell->setState(StreamingState::Unloaded);
                }
            }
        }
    }
}

void WorldPartitionManager::enforceCellBudget() {
    uint32_t activeCount = 0;
    for (const auto& [id, cell] : m_partition.getAllCells()) {
        if (cell && cell->isActive()) {
            ++activeCount;
        }
    }

    if (activeCount > m_config.maxActiveCells) {
        // Collect active cells sorted by distance (farthest first)
        std::vector<WorldPartitionCell*> candidates;
        candidates.reserve(activeCount);
        for (const auto& [id, cell] : m_partition.getAllCells()) {
            if (cell && cell->isActive()) {
                candidates.push_back(cell.get());
            }
        }

        std::sort(candidates.begin(), candidates.end(), [](const WorldPartitionCell* a, const WorldPartitionCell* b) {
            return a->getDistanceToSource() > b->getDistanceToSource();
        });

        const uint32_t toEvict = activeCount - m_config.maxActiveCells;
        for (uint32_t i = 0; i < toEvict && i < candidates.size(); ++i) {
            candidates[i]->deactivate();
        }
    }
}

void WorldPartitionManager::updateStats() {
    m_stats.totalCells = static_cast<uint32_t>(m_partition.getCellCount());
    m_stats.loadedCells = 0;
    m_stats.activeCells = 0;
    m_stats.inactiveCells = 0;
    m_stats.pendingLoadingCells = 0;
    m_stats.pendingUnloadingCells = 0;
    m_stats.totalTerrainChunks = 0;
    m_stats.totalFoliageGroups = 0;
    m_stats.cellMemoryUsageBytes = 0;

    for (const auto& [id, cell] : m_partition.getAllCells()) {
        if (!cell) continue;

        switch (cell->getState()) {
            case StreamingState::Active:
                m_stats.activeCells++;
                m_stats.loadedCells++;
                break;
            case StreamingState::Resident:
                m_stats.loadedCells++;
                m_stats.inactiveCells++;
                break;
            case StreamingState::QueuedForLoading:
            case StreamingState::Loading:
            case StreamingState::CPUReady:
            case StreamingState::UploadQueued:
                m_stats.pendingLoadingCells++;
                break;
            case StreamingState::QueuedForUnloading:
            case StreamingState::Unloading:
                m_stats.pendingUnloadingCells++;
                break;
            case StreamingState::Unloaded:
            case StreamingState::Failed:
                m_stats.inactiveCells++;
                break;
        }

        m_stats.totalTerrainChunks += static_cast<uint32_t>(cell->getTerrainChunks().size());
        if (cell->getFoliageInstanceCount() > 0) {
            m_stats.totalFoliageGroups++;
        }
        m_stats.cellMemoryUsageBytes += cell->getMemoryUsageBytes();
    }

    m_stats.totalObjects = static_cast<uint32_t>(m_partition.getObjectCount());
    m_stats.avgObjectsPerCell = (m_stats.totalCells > 0)
        ? static_cast<float>(m_stats.totalObjects) / static_cast<float>(m_stats.totalCells)
        : 0.0f;
}

std::vector<DebugCellBox> WorldPartitionManager::getDebugCellBoxes() const {
    std::vector<DebugCellBox> boxes;
    boxes.reserve(m_partition.getCellCount());

    for (const auto& [id, cell] : m_partition.getAllCells()) {
        if (!cell) continue;

        glm::vec4 color{0.45f, 0.45f, 0.50f, 0.60f}; // Default Unloaded (Gray)
        switch (cell->getState()) {
            case StreamingState::Active:
                color = glm::vec4(0.10f, 0.95f, 0.20f, 1.0f); // Green
                break;
            case StreamingState::Resident:
                color = glm::vec4(0.20f, 0.80f, 0.85f, 1.0f); // Cyan
                break;
            case StreamingState::CPUReady:
                color = glm::vec4(0.80f, 0.95f, 0.20f, 1.0f); // Lime
                break;
            case StreamingState::Loading:
            case StreamingState::UploadQueued:
                color = glm::vec4(0.95f, 0.85f, 0.10f, 1.0f); // Yellow
                break;
            case StreamingState::QueuedForLoading:
                color = glm::vec4(0.85f, 0.65f, 0.10f, 1.0f); // Gold
                break;
            case StreamingState::QueuedForUnloading:
            case StreamingState::Unloading:
                color = glm::vec4(0.95f, 0.45f, 0.10f, 1.0f); // Orange
                break;
            case StreamingState::Failed:
                color = glm::vec4(0.95f, 0.10f, 0.10f, 1.0f); // Red
                break;
            case StreamingState::Unloaded:
            default:
                color = glm::vec4(0.45f, 0.45f, 0.50f, 0.60f); // Gray
                break;
        }

        boxes.push_back(DebugCellBox{
            .bounds = cell->getBounds(),
            .color = color,
            .cellId = cell->getId(),
            .coord = cell->getCoord(),
            .state = cell->getState(),
            .isActive = cell->isActive()
        });
    }

    return boxes;
}

} // namespace sidera
