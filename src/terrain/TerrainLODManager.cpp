#include "terrain/TerrainLODManager.h"
#include "core/Log.h"
#include "renderer/Renderer.h"
#include "terrain/HeightmapData.h"
#include "partition/WorldPartitionManager.h"
#include "streaming/WorldStreamingManager.h"

#include <algorithm>

namespace sidera {

namespace {

// 128-byte push constants matching shaders/terrain.vert and shaders/terrain.frag
struct TerrainLODPushConstants {
    glm::mat4 model{1.0f};
    glm::vec4 terrainParams{120.0f, 120.0f, 14.0f, 24.0f}; // x = width, y = length, z = heightScale, w = textureTiling
    glm::vec4 viewportParams{0.0f, 0.0f, 0.1f, 100.0f};     // x = mode, y = normalSpace, z = near, w = far
    glm::vec4 lodParams{0.0f, 0.0f, 0.0f, 0.0f};            // x = morphFactor, y = lodLevel, z = debugOverlayMode, w = stitchMask
    glm::vec4 edgeMorph{0.0f, 0.0f, 0.0f, 0.0f};            // x = northMorph, y = southMorph, z = westMorph, w = eastMorph
};
static_assert(sizeof(TerrainLODPushConstants) == 128, "TerrainLODPushConstants must be 128 bytes.");

constexpr uint64_t kDeferredDeleteFrames = 3ULL;
constexpr uint64_t kCacheEvictAgeFrames  = 180ULL;

} // namespace

TerrainLODManager::~TerrainLODManager() {
    destroy();
}

bool TerrainLODManager::init(const TerrainConfig& terrainConfig,
                             const TerrainLODConfig& lodConfig,
                             const HeightmapData& heightmap,
                             const VulkanDevice* device,
                             VkCommandPool transferCommandPool) {
    destroy();

    m_terrainConfig = terrainConfig;
    m_lodConfig = lodConfig;
    m_lodConfig.validate();
    m_heightmap = &heightmap;
    m_device = device;
    m_transferCommandPool = transferCommandPool;

    m_indexPool.init(m_lodConfig.patchResolution, m_device, m_transferCommandPool);
    m_shadowTree.init(m_terrainConfig, m_lodConfig, *m_heightmap);

    m_totalGpuMemoryBytes = m_indexPool.getGpuMemoryBytes();
    m_frameIndex = 0;
    m_initialized = true;

    SIDERA_LOG_INFO(
        "TerrainLOD",
        "Initialized Quadtree Terrain LOD (res: {}x{}, depth: {}..{}, errThresh: {:.2f}px, strategy: {}).",
        m_lodConfig.patchResolution,
        m_lodConfig.patchResolution,
        m_lodConfig.minDepth,
        m_lodConfig.maxDepth,
        m_lodConfig.pixelErrorThreshold,
        getTerrainLODStrategyName(m_lodConfig.strategy)
    );
    return true;
}

void TerrainLODManager::destroy() noexcept {
    m_viewportTrees.clear();
    m_viewportSelections.clear();
    m_shadowTree.clear();
    m_shadowSelection = {};
    m_patchCache.clear();
    m_deferredDeletions.clear();
    m_indexPool.destroy();
    m_totalGpuMemoryBytes = 0;
    m_initialized = false;
}

void TerrainLODManager::beginFrame() noexcept {
    ++m_frameIndex;
    flushDeferredDeletions();
    if ((m_frameIndex % 60ULL) == 0ULL) {
        evictUnusedPatches();
    }
}

void TerrainLODManager::flushDeferredDeletions() noexcept {
    auto it = m_deferredDeletions.begin();
    while (it != m_deferredDeletions.end()) {
        if (m_frameIndex >= it->retireFrame) {
            if (it->mesh) {
                const uint64_t bytes = it->mesh->getGpuMemoryBytes();
                if (m_totalGpuMemoryBytes >= bytes) {
                    m_totalGpuMemoryBytes -= bytes;
                }
            }
            it = m_deferredDeletions.erase(it);
        } else {
            ++it;
        }
    }
}

void TerrainLODManager::evictUnusedPatches() {
    auto it = m_patchCache.begin();
    while (it != m_patchCache.end()) {
        if (m_frameIndex > it->second.lastUsedFrame + kCacheEvictAgeFrames) {
            m_deferredDeletions.push_back(DeferredDeletion{
                .mesh = std::move(it->second.mesh),
                .retireFrame = m_frameIndex + kDeferredDeleteFrames
            });
            it = m_patchCache.erase(it);
        } else {
            ++it;
        }
    }
}

TerrainPatchMesh* TerrainLODManager::getOrCreatePatchMesh(const TerrainPatchSelectionEntry& entry) {
    auto it = m_patchCache.find(entry.nodeId);
    if (it != m_patchCache.end()) {
        it->second.lastUsedFrame = m_frameIndex;
        return it->second.mesh.get();
    }

    if (!m_heightmap || !m_heightmap->isValid()) {
        return nullptr;
    }

    auto patch = std::make_unique<TerrainPatchMesh>();
    patch->generate(
        m_lodConfig.patchResolution,
        entry.uMin, entry.uMax,
        entry.vMin, entry.vMax,
        entry.lodLevel,
        m_terrainConfig,
        *m_heightmap,
        m_device,
        m_transferCommandPool
    );

    m_totalGpuMemoryBytes += patch->getGpuMemoryBytes();
    TerrainPatchMesh* rawPtr = patch.get();
    m_patchCache.emplace(entry.nodeId, CachedPatch{
        .mesh = std::move(patch),
        .lastUsedFrame = m_frameIndex
    });
    return rawPtr;
}

const TerrainLODSelection& TerrainLODManager::updateViewport(uint32_t viewportId,
                                                             float deltaTime,
                                                             const Camera& camera,
                                                             uint32_t viewportHeight,
                                                             bool frustumCull) {
    auto& tree = m_viewportTrees[viewportId];
    if (!tree.isInitialized() && m_heightmap) {
        tree.init(m_terrainConfig, m_lodConfig, *m_heightmap);
    }

    tree.update(deltaTime, camera, viewportHeight, frustumCull, 1.0f);
    TerrainLODSelection sel = tree.buildSelection(m_lodConfig.patchResolution);

    // Ensure GPU patch meshes exist for all visible patches and compute exact 64-bit triangle counts
    uint64_t exactVerts = 0;
    uint64_t exactTris = 0;
    for (const auto& entry : sel.visiblePatches) {
        TerrainPatchMesh* mesh = getOrCreatePatchMesh(entry);
        if (mesh) {
            exactVerts += static_cast<uint64_t>(mesh->getVertexCount());
            exactTris += static_cast<uint64_t>(m_indexPool.getTriangleCount(entry.stitchMask));
        }
    }

    sel.stats.totalTerrainVertices = exactVerts;
    sel.stats.totalTerrainTriangles = exactTris;
    sel.stats.cachedPatchCount = static_cast<uint32_t>(m_patchCache.size());
    sel.stats.terrainGpuMemoryBytes = m_totalGpuMemoryBytes;

    m_viewportSelections[viewportId] = std::move(sel);
    return m_viewportSelections[viewportId];
}

const TerrainLODSelection& TerrainLODManager::updateShadow(float deltaTime,
                                                           const Camera& camera,
                                                           uint32_t viewportHeight) {
    if (!m_shadowTree.isInitialized() && m_heightmap) {
        m_shadowTree.init(m_terrainConfig, m_lodConfig, *m_heightmap);
    }

    // Shadow pass uses configurable shadow LOD bias and disables camera frustum cull so off-screen casters cast shadows
    m_shadowTree.update(deltaTime, camera, viewportHeight, false, m_lodConfig.shadowLodBias);
    m_shadowSelection = m_shadowTree.buildSelection(m_lodConfig.patchResolution);

    for (const auto& entry : m_shadowSelection.visiblePatches) {
        (void)getOrCreatePatchMesh(entry);
    }
    return m_shadowSelection;
}

uint32_t TerrainLODManager::renderViewport(VkCommandBuffer cmd,
                                           VkPipelineLayout pipelineLayout,
                                           uint32_t viewportId,
                                           const glm::vec4& vpParams) const noexcept {
    if (!m_initialized) {
        return 0;
    }

    auto selIt = m_viewportSelections.find(viewportId);
    if (selIt == m_viewportSelections.end()) {
        selIt = m_viewportSelections.find(0u);
        if (selIt == m_viewportSelections.end()) {
            return 0;
        }
    }

    const auto& selection = selIt->second;
    uint32_t drawCalls = 0;

    for (const auto& entry : selection.visiblePatches) {
        const TerrainPatchMesh* mesh = getCachedPatchMesh(entry.nodeId);
        if (!mesh || !mesh->isGpuResident()) {
            continue;
        }

        const TerrainLODPushConstants pc{
            .model = glm::mat4(1.0f),
            .terrainParams = glm::vec4(
                m_terrainConfig.width,
                m_terrainConfig.length,
                m_terrainConfig.heightScale,
                m_terrainConfig.textureTiling
            ),
            .viewportParams = vpParams,
            .lodParams = glm::vec4(
                m_lodConfig.geomorphingEnabled ? entry.morphFactor : 0.0f,
                static_cast<float>(entry.lodLevel),
                static_cast<float>(m_lodConfig.debugOverlayMode),
                static_cast<float>(entry.stitchMask)
            ),
            .edgeMorph = m_lodConfig.geomorphingEnabled ? entry.edgeMorph : glm::vec4(0.0f)
        };

        vkCmdPushConstants(
            cmd,
            pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(TerrainLODPushConstants),
            &pc
        );

        mesh->bindAndDraw(cmd, m_indexPool, entry.stitchMask);
        ++drawCalls;
    }

    return drawCalls;
}

uint32_t TerrainLODManager::renderShadow(VkCommandBuffer cmd,
                                         VkPipelineLayout shadowLayout,
                                         const glm::mat4& lightSpaceMatrix) const noexcept {
    if (!m_initialized) {
        return 0;
    }

    const ShadowPushConstants shadowPc{
        .lightSpaceMatrix = lightSpaceMatrix,
        .model = glm::mat4(1.0f)
    };

    vkCmdPushConstants(
        cmd,
        shadowLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(ShadowPushConstants),
        &shadowPc
    );

    uint32_t drawCalls = 0;
    for (const auto& entry : m_shadowSelection.visiblePatches) {
        const TerrainPatchMesh* mesh = getCachedPatchMesh(entry.nodeId);
        if (!mesh || !mesh->isGpuResident()) {
            continue;
        }
        mesh->bindAndDraw(cmd, m_indexPool, entry.stitchMask);
        ++drawCalls;
    }
    return drawCalls;
}

void TerrainLODManager::setConfig(const TerrainLODConfig& cfg) {
    const uint32_t prevRes = m_lodConfig.patchResolution;
    m_lodConfig = cfg;
    m_lodConfig.validate();

    if (m_initialized && m_lodConfig.patchResolution != prevRes) {
        // Resolution changed: retire old patch meshes safely and rebuild shared index pool
        for (auto& [id, cached] : m_patchCache) {
            m_deferredDeletions.push_back(DeferredDeletion{
                .mesh = std::move(cached.mesh),
                .retireFrame = m_frameIndex + kDeferredDeleteFrames
            });
        }
        m_patchCache.clear();
        m_indexPool.init(m_lodConfig.patchResolution, m_device, m_transferCommandPool);
    }

    for (auto& [vpId, tree] : m_viewportTrees) {
        tree.setLODConfig(m_lodConfig);
    }
    m_shadowTree.setLODConfig(m_lodConfig);
}

void TerrainLODManager::applyQualityPreset(TerrainQualityPreset preset) {
    m_qualityPreset = preset;
    TerrainLODConfig cfg = TerrainLODConfig::fromPreset(preset);
    cfg.debugVisualization = m_lodConfig.debugVisualization;
    cfg.debugOverlayMode = m_lodConfig.debugOverlayMode;
    cfg.strategy = m_lodConfig.strategy;
    setConfig(cfg);
    SIDERA_LOG_INFO("TerrainLOD", "Applied Terrain LOD quality preset: {}.", getTerrainQualityPresetName(preset));
}

void TerrainLODManager::toggleStrategy() noexcept {
    m_lodConfig.strategy = (m_lodConfig.strategy == TerrainLODStrategy::ScreenSpaceError)
        ? TerrainLODStrategy::DistanceBased
        : TerrainLODStrategy::ScreenSpaceError;
    for (auto& [vpId, tree] : m_viewportTrees) {
        tree.setLODConfig(m_lodConfig);
    }
    m_shadowTree.setLODConfig(m_lodConfig);
    SIDERA_LOG_INFO("TerrainLOD", "Switched Terrain LOD strategy to: {}.", getTerrainLODStrategyName(m_lodConfig.strategy));
}

void TerrainLODManager::toggleDebugVisualization() noexcept {
    m_lodConfig.debugVisualization = !m_lodConfig.debugVisualization;
    m_lodConfig.debugOverlayMode = m_lodConfig.debugVisualization ? 1u : 0u;
    SIDERA_LOG_INFO("TerrainLOD", "Terrain LOD debug visualization: {}.", m_lodConfig.debugVisualization ? "ON" : "OFF");
}

void TerrainLODManager::cycleDebugOverlayMode() noexcept {
    m_lodConfig.debugOverlayMode = (m_lodConfig.debugOverlayMode + 1u) % 4u;
    m_lodConfig.debugVisualization = (m_lodConfig.debugOverlayMode != 0u);
    SIDERA_LOG_INFO("TerrainLOD", "Terrain LOD debug overlay mode: {}.", m_lodConfig.debugOverlayMode);
}

const TerrainLODStatistics& TerrainLODManager::getStats(uint32_t viewportId) const noexcept {
    static const TerrainLODStatistics s_empty{};
    auto it = m_viewportSelections.find(viewportId);
    if (it != m_viewportSelections.end()) {
        return it->second.stats;
    }
    auto it0 = m_viewportSelections.find(0u);
    return (it0 != m_viewportSelections.end()) ? it0->second.stats : s_empty;
}

const TerrainQuadtree* TerrainLODManager::getQuadtree(uint32_t viewportId) const noexcept {
    auto it = m_viewportTrees.find(viewportId);
    return (it != m_viewportTrees.end()) ? &it->second : nullptr;
}

const TerrainPatchMesh* TerrainLODManager::getCachedPatchMesh(uint64_t nodeId) const noexcept {
    auto it = m_patchCache.find(nodeId);
    return (it != m_patchCache.end()) ? it->second.mesh.get() : nullptr;
}

std::vector<TerrainDebugBox> TerrainLODManager::getDebugBoxes(uint32_t viewportId) const {
    auto it = m_viewportTrees.find(viewportId);
    if (it != m_viewportTrees.end()) {
        return it->second.getDebugBoxes();
    }
    return {};
}

} // namespace sidera
