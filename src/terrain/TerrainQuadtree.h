#pragma once

#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "terrain/TerrainConfig.h"
#include "terrain/TerrainLODConfig.h"
#include "terrain/TerrainLODStatistics.h"
#include "terrain/TerrainQuadNode.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sidera {

class HeightmapData;

// Selected renderable patch descriptor produced by TerrainQuadtree for a specific camera/viewport
struct TerrainPatchSelectionEntry {
    uint64_t nodeId = 0;
    uint32_t depth = 0;
    uint32_t lodLevel = 0;
    uint32_t gridX = 0;
    uint32_t gridZ = 0;
    float uMin = 0.0f;
    float uMax = 1.0f;
    float vMin = 0.0f;
    float vMax = 1.0f;
    AABB worldBounds{};
    float geometricError = 0.0f;
    float screenSpaceError = 0.0f;
    float morphFactor = 0.0f;
    uint8_t stitchMask = kEdgeNone;
    bool isVisible = true;
};

struct TerrainLODSelection {
    std::vector<TerrainPatchSelectionEntry> visiblePatches;
    std::vector<TerrainPatchSelectionEntry> allLeafPatches;
    TerrainLODStatistics stats{};
};

struct TerrainDebugBox {
    AABB bounds{};
    glm::vec4 color{0.0f, 1.0f, 0.0f, 1.0f};
    uint32_t lodLevel = 0;
};

class TerrainQuadtree {
public:
    TerrainQuadtree() = default;
    ~TerrainQuadtree() = default;

    // Initialize root node from terrain config and heightmap
    void init(const TerrainConfig& terrainConfig,
              const TerrainLODConfig& lodConfig,
              const HeightmapData& heightmap);

    void clear() noexcept;

    // Update persistent quadtree for a camera/viewport (subdivide, merge, balance, neighbors, geomorph, frustum cull)
    void update(float deltaTime,
                const Camera& camera,
                uint32_t viewportHeight,
                bool frustumCull = true,
                float errorBiasMultiplier = 1.0f);

    // Extract current selection from the persistent quadtree
    [[nodiscard]] TerrainLODSelection buildSelection(uint32_t patchResolution) const;

    // Pure screen-space geometric error calculation (exposed for unit testing and diagnostics)
    [[nodiscard]] static float computeScreenSpaceError(float geometricError,
                                                       float distance,
                                                       float fovDegrees,
                                                       uint32_t viewportHeight) noexcept;

    // Shortest distance from a point to an AABB (0 if inside)
    [[nodiscard]] static float distancePointToAABB(const glm::vec3& point, const AABB& box) noexcept;

    // Lookup active leaf node covering grid coordinate (gridX, gridZ) at or above targetDepth
    [[nodiscard]] uint32_t findLeafAt(uint32_t depth, int32_t gridX, int32_t gridZ) const noexcept;

    // Debug visualization bounding boxes colored by LOD level
    [[nodiscard]] std::vector<TerrainDebugBox> getDebugBoxes() const;

    // Accessors
    [[nodiscard]] const std::vector<TerrainQuadNode>& getNodes() const noexcept { return m_nodes; }
    [[nodiscard]] const TerrainQuadNode* getRoot() const noexcept { return m_nodes.empty() ? nullptr : &m_nodes[0]; }
    [[nodiscard]] const TerrainLODConfig& getLODConfig() const noexcept { return m_lodConfig; }
    void setLODConfig(const TerrainLODConfig& cfg) noexcept {
        m_lodConfig = cfg;
        m_lodConfig.validate();
    }
    [[nodiscard]] const TerrainLODStatistics& getStats() const noexcept { return m_stats; }
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    // Validate that all adjacent leaf nodes satisfy |depthA - depthB| <= 1
    [[nodiscard]] bool validateNeighborBalance() const noexcept;

private:
    uint32_t allocateNode(uint32_t parentIndex, uint32_t depth, uint32_t gridX, uint32_t gridZ);
    void computeNodeBoundsAndError(TerrainQuadNode& node) const;
    bool shouldSubdivide(const TerrainQuadNode& node,
                         const glm::vec3& cameraPos,
                         float fovDegrees,
                         uint32_t viewportHeight,
                         float errorBiasMultiplier,
                         bool currentlySplit) const noexcept;
    void subdivideNode(uint32_t nodeIndex, bool startMorphed);
    void mergeNode(uint32_t nodeIndex);
    void evaluateRecursive(uint32_t nodeIndex,
                           const glm::vec3& cameraPos,
                           float fovDegrees,
                           uint32_t viewportHeight,
                           float errorBiasMultiplier,
                           uint32_t& activeLeafBudget);
    void balanceQuadtree();
    void updateNeighborsAndStitchMasks();
    void updateGeomorphing(float deltaTime);
    void updateVisibilityAndStats(const Frustum& frustum, bool frustumCull);

    TerrainConfig m_terrainConfig{};
    TerrainLODConfig m_lodConfig{};
    const HeightmapData* m_heightmap = nullptr;

    std::vector<TerrainQuadNode> m_nodes;
    std::vector<uint32_t> m_freeNodeIndices;
    std::vector<uint32_t> m_activeLeaves;
    std::unordered_map<uint64_t, uint32_t> m_keyToIndex;

    TerrainLODStatistics m_stats{};
    bool m_initialized = false;
};

} // namespace sidera
