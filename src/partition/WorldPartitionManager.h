#pragma once

#include "partition/WorldPartition.h"
#include "partition/WorldPartitionConfig.h"
#include "partition/WorldPartitionStats.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "streaming/StreamingSource.h"
#include "streaming/StreamingState.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace sidera {

class WorldStreamingManager;
class Terrain;
class FoliageSystem;

struct DebugCellBox {
    AABB bounds{};
    glm::vec4 color{1.0f};
    uint64_t cellId = 0;
    CellCoord coord{0, 0};
    StreamingState state = StreamingState::Unloaded;
    bool isActive = false;
};

// Central manager coordinating spatial partition, object life cycles, streaming requests, and debug visualization
class WorldPartitionManager {
public:
    WorldPartitionManager();
    ~WorldPartitionManager();

    WorldPartitionManager(const WorldPartitionManager&) = delete;
    WorldPartitionManager& operator=(const WorldPartitionManager&) = delete;

    // Initialization
    bool init(const WorldPartitionConfig& config,
              WorldStreamingManager* streamingManager = nullptr,
              Terrain* terrain = nullptr,
              FoliageSystem* foliageSystem = nullptr);

    // Frame update on the render/simulation thread
    void update(float deltaTime, const Camera& camera);

    // Observer / Streaming Sources
    uint32_t registerSource(const StreamingSource& source);
    void unregisterSource(uint32_t sourceId);
    void updateSource(uint32_t sourceId, const glm::vec3& position, const glm::vec3& direction, const glm::vec3& velocity);
    [[nodiscard]] const std::vector<StreamingSource>& getSources() const noexcept { return m_sources; }

    // Object Management
    uint32_t registerObject(const std::string& name,
                            PartitionObjectType type,
                            const glm::vec3& position,
                            const AABB& bounds,
                            void* userPointer = nullptr,
                            uint64_t memoryBytes = 0);
    bool unregisterObject(uint32_t objectId);
    bool moveObject(uint32_t objectId, const glm::vec3& newPosition, const AABB& newBounds);
    [[nodiscard]] const WorldPartitionObject* getObject(uint32_t objectId) const;

    // Terrain & Foliage Integration
    bool registerTerrainChunk(TerrainChunk* chunk);
    bool unregisterTerrainChunk(uint32_t chunkId);
    bool registerFoliageToCell(uint64_t cellId, const std::vector<uint32_t>& instanceIds);

    // Spatial Query API (with performance metrics tracking)
    [[nodiscard]] std::shared_ptr<WorldPartitionCell> getCellAtPosition(const glm::vec3& worldPos);
    [[nodiscard]] std::vector<std::shared_ptr<WorldPartitionCell>> getCellsInRadius(const glm::vec3& worldPos, float radius);
    [[nodiscard]] std::vector<std::shared_ptr<WorldPartitionCell>> getCellsInBounds(const AABB& aabb);
    [[nodiscard]] std::vector<std::shared_ptr<WorldPartitionCell>> getNeighborCells(const CellCoord& coord, int32_t radius = 1);
    [[nodiscard]] std::vector<const WorldPartitionObject*> getObjectsInBounds(const AABB& aabb);
    [[nodiscard]] std::vector<TerrainChunk*> getTerrainInBounds(const AABB& aabb);
    [[nodiscard]] std::vector<uint32_t> getFoliageInBounds(const AABB& aabb);

    // Configuration & State
    [[nodiscard]] const WorldPartitionConfig& getConfig() const noexcept { return m_config; }
    void setConfig(const WorldPartitionConfig& config);
    [[nodiscard]] bool isEnabled() const noexcept { return m_config.enabled; }
    void setEnabled(bool enabled) noexcept { m_config.enabled = enabled; }
    void toggleEnabled() noexcept { m_config.enabled = !m_config.enabled; }

    // Debug Visualization
    [[nodiscard]] bool isVisualizationEnabled() const noexcept { return m_config.debugVisualization; }
    void setVisualizationEnabled(bool enabled) noexcept { m_config.debugVisualization = enabled; }
    void toggleVisualization() noexcept { m_config.debugVisualization = !m_config.debugVisualization; }
    [[nodiscard]] std::vector<DebugCellBox> getDebugCellBoxes() const;

    // Telemetry & Statistics
    [[nodiscard]] const WorldPartitionStats& getStats() const noexcept { return m_stats; }
    [[nodiscard]] const WorldPartition& getPartition() const noexcept { return m_partition; }
    [[nodiscard]] WorldPartition& getPartition() noexcept { return m_partition; }

private:
    void synchronizeWithStreaming();
    void evaluateHysteresisAndStreaming(const Camera& camera, const Frustum& frustum);
    void enforceCellBudget();
    void updateStats();

    WorldPartitionConfig m_config{};
    WorldPartition m_partition;
    WorldStreamingManager* m_streamingManager = nullptr;
    Terrain* m_terrain = nullptr;
    FoliageSystem* m_foliageSystem = nullptr;

    std::vector<StreamingSource> m_sources;
    mutable WorldPartitionStats m_stats{};
    glm::vec3 m_lastCameraPos{0.0f};
    uint64_t m_frameIndex = 0;
    bool m_initialized = false;
};

} // namespace sidera
