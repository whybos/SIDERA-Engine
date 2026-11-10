#pragma once

#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "streaming/StreamingConfig.h"
#include "streaming/StreamingRegion.h"
#include "streaming/StreamingSource.h"
#include "streaming/StreamingStats.h"
#include "streaming/StreamingUploadQueue.h"
#include "terrain/HeightmapData.h"
#include "terrain/TerrainConfig.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sidera {

class Terrain;
class FoliageSystem;
class VulkanDevice;

// Master coordinator for streaming world regions, async loading, memory budgets, and hysteresis
class WorldStreamingManager {
public:
    WorldStreamingManager();
    ~WorldStreamingManager();

    WorldStreamingManager(const WorldStreamingManager&) = delete;
    WorldStreamingManager& operator=(const WorldStreamingManager&) = delete;

    // Initialization for live rendering with Vulkan
    bool init(const StreamingConfig& config,
              const TerrainConfig& terrainConfig,
              const HeightmapData& heightmap,
              const VulkanDevice* device,
              VkCommandPool transferCommandPool,
              Terrain* terrain,
              FoliageSystem* foliageSystem);

    // Headless initialization for testing (no GPU allocations)
    bool initHeadless(const StreamingConfig& config,
                      const TerrainConfig& terrainConfig,
                      const HeightmapData& heightmap);

    // Frame update called on the main render thread
    void update(float deltaTime, const Camera& camera);

    // Worker threads and queue shutdown
    void shutdown();

    // Source management
    uint32_t registerSource(const StreamingSource& source);
    void unregisterSource(uint32_t sourceId);
    void updateSource(uint32_t sourceId, const glm::vec3& position, const glm::vec3& direction, const glm::vec3& velocity);
    [[nodiscard]] const std::vector<StreamingSource>& getSources() const noexcept { return m_sources; }

    // Region queries
    [[nodiscard]] StreamingRegion* getRegion(uint32_t regionId) noexcept;
    [[nodiscard]] const StreamingRegion* getRegion(uint32_t regionId) const noexcept;
    [[nodiscard]] StreamingRegion* getRegionAt(float worldX, float worldZ) noexcept;
    [[nodiscard]] const std::vector<std::unique_ptr<StreamingRegion>>& getRegions() const noexcept { return m_regions; }

    // Manual controls / testing
    void forceLoadRegion(uint32_t regionId);
    void forceUnloadRegion(uint32_t regionId);
    void forceLoadAll();
    void forceUnloadAll();

    // Configuration & State
    [[nodiscard]] const StreamingConfig& getConfig() const noexcept { return m_config; }
    void setConfig(const StreamingConfig& config);
    [[nodiscard]] bool isEnabled() const noexcept { return m_config.enabled; }
    void setEnabled(bool enabled) noexcept { m_config.enabled = enabled; }
    void toggleEnabled() noexcept { m_config.enabled = !m_config.enabled; }

    [[nodiscard]] const StreamingStats& getStats() const noexcept { return m_stats; }
    [[nodiscard]] uint64_t getFrameIndex() const noexcept { return m_frameIndex; }

private:
    void createGrid();
    void startWorkerThreads();
    void stopWorkerThreads();
    void workerThreadLoop();

    void enqueueCpuLoad(uint32_t regionId);
    void processCompletedCpuLoads();
    void evaluateHysteresis(const Camera& camera, const Frustum& frustum);
    void enforceMemoryBudget();
    void updateStats();

    StreamingConfig m_config{};
    TerrainConfig m_terrainConfig{};
    HeightmapData m_heightmap{};

    const VulkanDevice* m_device = nullptr;
    VkCommandPool m_transferCommandPool = VK_NULL_HANDLE;
    Terrain* m_terrain = nullptr;
    FoliageSystem* m_foliageSystem = nullptr;

    std::vector<std::unique_ptr<StreamingRegion>> m_regions;
    std::vector<StreamingSource> m_sources;
    StreamingUploadQueue m_uploadQueue;
    mutable StreamingStats m_stats{};

    // Threading & queues
    std::vector<std::jthread> m_workerThreads;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::deque<uint32_t> m_cpuLoadQueue;

    std::mutex m_completedMutex;
    std::vector<uint32_t> m_cpuCompletedList;

    std::atomic<bool> m_stopWorkers{false};
    uint64_t m_frameIndex = 0;
    glm::vec3 m_lastCameraPos{0.0f};
    bool m_initialized = false;
    bool m_headless = false;
};

} // namespace sidera
