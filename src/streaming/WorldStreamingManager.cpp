#include "streaming/WorldStreamingManager.h"
#include "terrain/Terrain.h"
#include "foliage/FoliageSystem.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>

namespace sidera {

WorldStreamingManager::WorldStreamingManager() = default;

WorldStreamingManager::~WorldStreamingManager() {
    shutdown();
}

bool WorldStreamingManager::init(const StreamingConfig& config,
                                 const TerrainConfig& terrainConfig,
                                 const HeightmapData& heightmap,
                                 const VulkanDevice* device,
                                 VkCommandPool transferCommandPool,
                                 Terrain* terrain,
                                 FoliageSystem* foliageSystem) {
    shutdown();

    m_config = config;
    m_config.validate();
    m_terrainConfig = terrainConfig;
    m_heightmap = heightmap;
    m_device = device;
    m_transferCommandPool = transferCommandPool;
    m_terrain = terrain;
    m_foliageSystem = foliageSystem;
    m_headless = false;

    createGrid();
    startWorkerThreads();

    // Register primary camera streaming source
    StreamingSource camSource{};
    camSource.id = 1;
    camSource.name = "PrimaryCamera";
    camSource.enabled = true;
    camSource.radiusMultiplier = 1.0f;
    camSource.priorityWeight = 1.0f;
    registerSource(camSource);

    m_initialized = true;
    SIDERA_LOG_INFO(
        "Streaming",
        "WorldStreamingManager initialized: {} regions ({}x{}), loadRadius: {:.1f}m, unloadRadius: {:.1f}m, hysteresis: {:.1f}m, workers: {}.",
        m_regions.size(),
        m_config.gridDimX,
        m_config.gridDimZ,
        m_config.loadRadius,
        m_config.unloadRadius,
        m_config.hysteresisMargin,
        m_config.maxConcurrentCpuJobs
    );

    return true;
}

bool WorldStreamingManager::initHeadless(const StreamingConfig& config,
                                         const TerrainConfig& terrainConfig,
                                         const HeightmapData& heightmap) {
    shutdown();

    m_config = config;
    m_config.validate();
    m_terrainConfig = terrainConfig;
    m_heightmap = heightmap;
    m_device = nullptr;
    m_transferCommandPool = VK_NULL_HANDLE;
    m_terrain = nullptr;
    m_foliageSystem = nullptr;
    m_headless = true;

    createGrid();
    startWorkerThreads();

    StreamingSource camSource{};
    camSource.id = 1;
    camSource.name = "TestCamera";
    camSource.enabled = true;
    registerSource(camSource);

    m_initialized = true;
    return true;
}

void WorldStreamingManager::shutdown() {
    stopWorkerThreads();

    if (!m_regions.empty()) {
        for (auto& region : m_regions) {
            if (region) {
                region->unloadCpu();
            }
        }
    }
    m_regions.clear();
    m_sources.clear();
    m_uploadQueue.clear();

    m_initialized = false;
    m_headless = false;
}

void WorldStreamingManager::createGrid() {
    m_regions.clear();
    const uint32_t total = m_config.gridDimX * m_config.gridDimZ;
    m_regions.reserve(total);

    const float worldW = static_cast<float>(m_config.gridDimX) * m_config.regionSize;
    const float worldL = static_cast<float>(m_config.gridDimZ) * m_config.regionSize;
    const float startX = -worldW * 0.5f;
    const float startZ = -worldL * 0.5f;

    uint32_t regionId = 1;
    for (uint32_t gz = 0; gz < m_config.gridDimZ; ++gz) {
        for (uint32_t gx = 0; gx < m_config.gridDimX; ++gx) {
            const float minX = startX + static_cast<float>(gx) * m_config.regionSize;
            const float maxX = minX + m_config.regionSize;
            const float minZ = startZ + static_cast<float>(gz) * m_config.regionSize;
            const float maxZ = minZ + m_config.regionSize;

            AABB bounds{};
            bounds.min = glm::vec3(minX, m_terrainConfig.heightOffset - 10.0f, minZ);
            bounds.max = glm::vec3(maxX, m_terrainConfig.heightOffset + m_terrainConfig.heightScale + 30.0f, maxZ);

            // Assign varied biome characteristics per grid cell
            RegionBiome biome = RegionBiome::LushMeadow;
            const uint32_t pattern = (gx + gz) % 6;
            switch (pattern) {
                case 0: biome = RegionBiome::PineForest; break;
                case 1: biome = RegionBiome::AlpineRidge; break;
                case 2: biome = RegionBiome::RockyPlateau; break;
                case 3: biome = RegionBiome::DenseWoodland; break;
                case 4: biome = RegionBiome::ValleyBasin; break;
                default: biome = RegionBiome::LushMeadow; break;
            }

            auto reg = std::make_unique<StreamingRegion>(
                regionId++,
                gx,
                gz,
                bounds,
                biome
            );

            if (m_terrain) {
                const uint32_t chunkId = gz * m_config.gridDimX + gx;
                TerrainChunk* chunk = m_terrain->getChunk(chunkId);
                if (chunk) {
                    reg->setLinkedChunk(chunk);
                }
            }

            m_regions.push_back(std::move(reg));
        }
    }
}

void WorldStreamingManager::startWorkerThreads() {
    m_stopWorkers = false;
    const uint32_t numThreads = std::max(1u, m_config.maxConcurrentCpuJobs);
    m_workerThreads.reserve(numThreads);

    for (uint32_t i = 0; i < numThreads; ++i) {
        m_workerThreads.emplace_back([this]() {
            workerThreadLoop();
        });
    }
}

void WorldStreamingManager::stopWorkerThreads() {
    m_stopWorkers = true;
    m_cv.notify_all();

    for (auto& t : m_workerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_workerThreads.clear();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_cpuLoadQueue.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_completedMutex);
        m_cpuCompletedList.clear();
    }
}

void WorldStreamingManager::workerThreadLoop() {
    while (!m_stopWorkers) {
        uint32_t regionId = 0;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this]() {
                return m_stopWorkers || !m_cpuLoadQueue.empty();
            });

            if (m_stopWorkers) {
                break;
            }

            regionId = m_cpuLoadQueue.front();
            m_cpuLoadQueue.pop_front();
        }

        StreamingRegion* region = getRegion(regionId);
        if (region && region->getState() == StreamingState::QueuedForLoading) {
            const bool ok = region->loadCpu(m_terrainConfig, m_heightmap, m_foliageSystem);
            if (ok) {
                std::lock_guard<std::mutex> lock(m_completedMutex);
                m_cpuCompletedList.push_back(regionId);
            }
        }
    }
}

void WorldStreamingManager::enqueueCpuLoad(uint32_t regionId) {
    StreamingRegion* region = getRegion(regionId);
    if (!region || region->getState() != StreamingState::Unloaded) {
        return;
    }

    region->setState(StreamingState::QueuedForLoading);
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        // Avoid duplicate queuing
        if (std::find(m_cpuLoadQueue.begin(), m_cpuLoadQueue.end(), regionId) == m_cpuLoadQueue.end()) {
            m_cpuLoadQueue.push_back(regionId);
        }
    }
    m_cv.notify_one();
}

void WorldStreamingManager::processCompletedCpuLoads() {
    std::vector<uint32_t> completed;
    {
        std::lock_guard<std::mutex> lock(m_completedMutex);
        completed.swap(m_cpuCompletedList);
    }

    for (const uint32_t id : completed) {
        StreamingRegion* region = getRegion(id);
        if (!region) continue;

        if (region->getState() == StreamingState::CPUReady) {
            if (m_headless) {
                // In headless mode, treat upload as immediate
                region->setState(StreamingState::Resident);
            } else {
                m_uploadQueue.enqueueUpload(region);
            }
            m_stats.totalLoadsCompleted++;
        }
    }
}

void WorldStreamingManager::update(float deltaTime, const Camera& camera) {
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
        const float maxSpeed = 50.0f; // Max realistic prediction speed (50 m/s)
        if (glm::length(vel) > maxSpeed) {
            vel = glm::normalize(vel) * maxSpeed;
        }
    }
    m_lastCameraPos = camPos;
    updateSource(1, camPos, camera.getForward(), vel);

    // 2. Transfer completed CPU loads to GPU staging upload queue
    processCompletedCpuLoads();

    // 3. Process throttled staging uploads on render thread
    if (!m_headless && m_device && m_transferCommandPool != VK_NULL_HANDLE) {
        const auto uploadRes = m_uploadQueue.processUploads(
            *m_device,
            m_transferCommandPool,
            m_config.maxUploadBytesPerFrame,
            m_config.maxUploadOpsPerFrame
        );
        m_stats.uploadedBytesLastFrame = uploadRes.bytesUploaded;
        m_stats.uploadOpsLastFrame = uploadRes.opsCompleted;

        // 4. Process safe deferred GPU resource releases
        m_uploadQueue.processDeferredReleases();
    } else {
        m_stats.uploadedBytesLastFrame = 0;
        m_stats.uploadOpsLastFrame = 0;
    }

    // 5. Evaluate distance-based hysteresis & view frustum priority
    const glm::mat4 vp = camera.getProjectionMatrix() * camera.getViewMatrix();
    const Frustum frustum = Frustum::fromViewProj(vp);
    evaluateHysteresis(camera, frustum);

    // 6. Enforce GPU memory budget
    enforceMemoryBudget();

    // 7. Update diagnostics
    updateStats();
}

void WorldStreamingManager::evaluateHysteresis(const Camera& camera, const Frustum& frustum) {
    (void)camera;

    for (auto& region : m_regions) {
        if (!region) continue;

        // Compute minimum distance to any active streaming source
        float minDist = std::numeric_limits<float>::max();
        for (const auto& src : m_sources) {
            if (!src.enabled) continue;

            const glm::vec3 predPos = src.getPredictedPosition(m_config.velocityPredictionSeconds);
            const glm::vec3 regionCenter = (region->getBounds().min + region->getBounds().max) * 0.5f;

            // Clamped distance to region bounding box
            const glm::vec3 closestPoint = glm::clamp(predPos, region->getBounds().min, region->getBounds().max);
            const float dist = glm::length(predPos - closestPoint);
            const float scaledDist = dist / std::max(0.01f, src.radiusMultiplier);

            minDist = std::min(minDist, scaledDist);
        }

        region->setDistanceToSource(minDist);

        // Compute priority: closer regions have higher priority; frustum intersection adds bonus
        const bool inFrustum = frustum.intersectsAABB(region->getBounds());
        float priority = std::max(0.0f, 2000.0f - minDist);
        if (inFrustum) {
            priority += m_config.frustumPriorityBonus;
        }
        region->setPriority(priority);

        // State Machine transitions with distance hysteresis:
        const StreamingState currentState = region->getState();

        // Loading trigger: distance <= loadRadius
        if (currentState == StreamingState::Unloaded) {
            if (minDist <= m_config.loadRadius) {
                enqueueCpuLoad(region->getId());
            }
        }
        // Activation trigger: Resident region entering active load radius
        else if (currentState == StreamingState::Resident) {
            if (minDist <= m_config.loadRadius) {
                region->activate(m_terrain, m_foliageSystem);
                region->setLastActiveFrame(m_frameIndex);
            } else if (minDist > m_config.unloadRadius) {
                // Distant resident region ready for unload
                region->releaseGpu(m_terrain, m_foliageSystem);
                region->unloadCpu();
                m_stats.totalUnloadsCompleted++;
            }
        }
        // Active region updating or unregistering
        else if (currentState == StreamingState::Active) {
            region->setLastActiveFrame(m_frameIndex);

            // Unload trigger: strictly distance > unloadRadius (hysteresis gap prevents thrashing)
            if (minDist > m_config.unloadRadius) {
                region->deactivate(m_terrain, m_foliageSystem);

                if (!m_headless) {
                    // Safe deferred release of GPU buffers
                    auto chunkMesh = region->getTerrainChunk() ? region->getTerrainChunk()->getMesh() : nullptr;
                    (void)chunkMesh;
                    m_uploadQueue.enqueueDeferredRelease([reg = region.get(), terr = m_terrain, fol = m_foliageSystem]() {
                        if (reg) {
                            reg->releaseGpu(terr, fol);
                            reg->unloadCpu();
                        }
                    }, 3);
                } else {
                    region->releaseGpu(m_terrain, m_foliageSystem);
                    region->unloadCpu();
                }

                region->setState(StreamingState::Unloaded);
                m_stats.totalUnloadsCompleted++;
            }
        }
    }
}

void WorldStreamingManager::enforceMemoryBudget() {
    uint64_t totalGpuMem = 0;
    for (const auto& reg : m_regions) {
        if (reg) {
            totalGpuMem += reg->getGpuMemoryBytes();
        }
    }

    if (totalGpuMem <= m_config.gpuMemoryBudgetBytes) {
        return;
    }

    // Identify candidate regions for eviction: Resident but not Active, ordered by lowest priority
    std::vector<StreamingRegion*> evictCandidates;
    for (const auto& reg : m_regions) {
        if (reg && reg->getState() == StreamingState::Resident) {
            evictCandidates.push_back(reg.get());
        }
    }

    std::sort(evictCandidates.begin(), evictCandidates.end(), [](const StreamingRegion* a, const StreamingRegion* b) {
        return a->getPriority() < b->getPriority(); // Lowest priority first
    });

    for (auto* reg : evictCandidates) {
        if (totalGpuMem <= m_config.gpuMemoryBudgetBytes) {
            break;
        }
        totalGpuMem -= reg->getGpuMemoryBytes();
        reg->releaseGpu(m_terrain, m_foliageSystem);
        reg->unloadCpu();
        m_stats.totalUnloadsCompleted++;
    }
}

void WorldStreamingManager::updateStats() {
    m_stats.totalRegions = static_cast<uint32_t>(m_regions.size());
    m_stats.unloadedCount = 0;
    m_stats.queuedLoadCount = 0;
    m_stats.loadingCount = 0;
    m_stats.cpuReadyCount = 0;
    m_stats.uploadQueuedCount = 0;
    m_stats.residentCount = 0;
    m_stats.activeCount = 0;
    m_stats.queuedUnloadCount = 0;
    m_stats.unloadingCount = 0;
    m_stats.failedCount = 0;

    m_stats.totalCpuMemoryBytes = 0;
    m_stats.totalGpuMemoryBytes = 0;
    m_stats.activeTerrainChunks = 0;
    m_stats.activeFoliageInstances = 0;
    m_stats.activeTriangles = 0;

    for (const auto& reg : m_regions) {
        if (!reg) continue;

        switch (reg->getState()) {
            case StreamingState::Unloaded:           m_stats.unloadedCount++; break;
            case StreamingState::QueuedForLoading:   m_stats.queuedLoadCount++; break;
            case StreamingState::Loading:            m_stats.loadingCount++; break;
            case StreamingState::CPUReady:           m_stats.cpuReadyCount++; break;
            case StreamingState::UploadQueued:       m_stats.uploadQueuedCount++; break;
            case StreamingState::Resident:           m_stats.residentCount++; break;
            case StreamingState::Active:             m_stats.activeCount++; break;
            case StreamingState::QueuedForUnloading: m_stats.queuedUnloadCount++; break;
            case StreamingState::Unloading:          m_stats.unloadingCount++; break;
            case StreamingState::Failed:             m_stats.failedCount++; break;
        }

        m_stats.totalCpuMemoryBytes += reg->getCpuMemoryBytes();
        m_stats.totalGpuMemoryBytes += reg->getGpuMemoryBytes();

        if (reg->getState() == StreamingState::Active) {
            if (reg->getTerrainChunk()) {
                m_stats.activeTerrainChunks++;
                m_stats.activeTriangles += reg->getTerrainChunk()->getTriangleCount();
            }
            m_stats.activeFoliageInstances += reg->getFoliageCount();
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stats.cpuQueueLength = static_cast<uint32_t>(m_cpuLoadQueue.size());
    }
    m_stats.uploadQueueLength = static_cast<uint32_t>(m_uploadQueue.getPendingCount());
    m_stats.activeStreamingSources = static_cast<uint32_t>(m_sources.size());
}

uint32_t WorldStreamingManager::registerSource(const StreamingSource& source) {
    uint32_t id = source.id;
    if (id == 0) {
        id = static_cast<uint32_t>(m_sources.size() + 1);
    }
    StreamingSource entry = source;
    entry.id = id;
    m_sources.push_back(entry);
    return id;
}

void WorldStreamingManager::unregisterSource(uint32_t sourceId) {
    auto it = std::remove_if(m_sources.begin(), m_sources.end(), [sourceId](const StreamingSource& s) {
        return s.id == sourceId;
    });
    m_sources.erase(it, m_sources.end());
}

void WorldStreamingManager::updateSource(uint32_t sourceId,
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

StreamingRegion* WorldStreamingManager::getRegion(uint32_t regionId) noexcept {
    for (auto& r : m_regions) {
        if (r && r->getId() == regionId) {
            return r.get();
        }
    }
    return nullptr;
}

const StreamingRegion* WorldStreamingManager::getRegion(uint32_t regionId) const noexcept {
    for (const auto& r : m_regions) {
        if (r && r->getId() == regionId) {
            return r.get();
        }
    }
    return nullptr;
}

StreamingRegion* WorldStreamingManager::getRegionAt(float worldX, float worldZ) noexcept {
    for (auto& r : m_regions) {
        if (r) {
            const AABB& b = r->getBounds();
            if (worldX >= b.min.x && worldX <= b.max.x && worldZ >= b.min.z && worldZ <= b.max.z) {
                return r.get();
            }
        }
    }
    return nullptr;
}

void WorldStreamingManager::forceLoadRegion(uint32_t regionId) {
    StreamingRegion* region = getRegion(regionId);
    if (!region) return;

    if (region->getState() == StreamingState::Unloaded) {
        region->loadCpu(m_terrainConfig, m_heightmap, m_foliageSystem);
    }
    if (!m_headless && m_device && m_transferCommandPool != VK_NULL_HANDLE) {
        region->uploadGpu(*m_device, m_transferCommandPool);
    } else {
        region->setState(StreamingState::Resident);
    }
    region->activate(m_terrain, m_foliageSystem);
}

void WorldStreamingManager::forceUnloadRegion(uint32_t regionId) {
    StreamingRegion* region = getRegion(regionId);
    if (!region) return;

    region->deactivate(m_terrain, m_foliageSystem);
    region->releaseGpu(m_terrain, m_foliageSystem);
    region->unloadCpu();
}

void WorldStreamingManager::forceLoadAll() {
    for (auto& r : m_regions) {
        if (r) {
            forceLoadRegion(r->getId());
        }
    }
}

void WorldStreamingManager::forceUnloadAll() {
    for (auto& r : m_regions) {
        if (r) {
            forceUnloadRegion(r->getId());
        }
    }
}

void WorldStreamingManager::setConfig(const StreamingConfig& config) {
    m_config = config;
    m_config.validate();
}

} // namespace sidera
