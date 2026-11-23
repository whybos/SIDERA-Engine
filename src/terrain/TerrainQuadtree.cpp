#include "terrain/TerrainQuadtree.h"
#include "terrain/HeightmapData.h"
#include "terrain/TerrainPatchMesh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>

namespace sidera {

float TerrainQuadtree::distancePointToAABB(const glm::vec3& point, const AABB& box) noexcept {
    const glm::vec3 clamped = glm::clamp(point, box.min, box.max);
    const glm::vec3 diff = point - clamped;
    return std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
}

float TerrainQuadtree::computeScreenSpaceError(float geometricError,
                                               float distance,
                                               float fovDegrees,
                                               uint32_t viewportHeight) noexcept {
    const float safeDist = std::max(distance, 0.05f);
    const float fovRad = fovDegrees * (std::numbers::pi_v<float> / 180.0f);
    const float tanHalfFov = std::max(std::tan(fovRad * 0.5f), 0.01f);
    const float k = static_cast<float>(std::max(viewportHeight, 1u)) / (2.0f * tanHalfFov);
    return (geometricError * k) / safeDist;
}

void TerrainQuadtree::init(const TerrainConfig& terrainConfig,
                           const TerrainLODConfig& lodConfig,
                           const HeightmapData& heightmap) {
    clear();
    m_terrainConfig = terrainConfig;
    m_lodConfig = lodConfig;
    m_lodConfig.validate();
    m_heightmap = &heightmap;

    m_nodes.reserve(1024);
    allocateNode(TerrainQuadNode::kInvalidIndex, 0u, 0u, 0u);
    m_initialized = true;
}

void TerrainQuadtree::clear() noexcept {
    m_nodes.clear();
    m_freeNodeIndices.clear();
    m_activeLeaves.clear();
    m_keyToIndex.clear();
    m_stats.reset();
    m_initialized = false;
}

uint32_t TerrainQuadtree::allocateNode(uint32_t parentIndex, uint32_t depth, uint32_t gridX, uint32_t gridZ) {
    uint32_t idx = 0;
    if (!m_freeNodeIndices.empty()) {
        idx = m_freeNodeIndices.back();
        m_freeNodeIndices.pop_back();
        m_nodes[idx] = TerrainQuadNode{};
    } else {
        idx = static_cast<uint32_t>(m_nodes.size());
        m_nodes.emplace_back();
    }

    TerrainQuadNode& node = m_nodes[idx];
    node.nodeId = makeTerrainNodeKey(depth, gridX, gridZ);
    node.parentIndex = parentIndex;
    node.depth = depth;
    node.gridX = gridX;
    node.gridZ = gridZ;
    node.lodLevel = (m_lodConfig.maxDepth >= depth) ? (m_lodConfig.maxDepth - depth) : 0u;
    node.isLeaf = true;
    node.isVisible = true;
    node.patchState = TerrainPatchState::Active;
    node.morphFactor = 0.0f;
    node.targetMorphFactor = 0.0f;

    const float invDim = 1.0f / static_cast<float>(1u << depth);
    node.uMin = static_cast<float>(gridX) * invDim;
    node.uMax = static_cast<float>(gridX + 1u) * invDim;
    node.vMin = static_cast<float>(gridZ) * invDim;
    node.vMax = static_cast<float>(gridZ + 1u) * invDim;

    computeNodeBoundsAndError(node);
    m_keyToIndex[node.nodeId] = idx;
    return idx;
}

void TerrainQuadtree::computeNodeBoundsAndError(TerrainQuadNode& node) const {
    const float minX = (node.uMin - 0.5f) * m_terrainConfig.width;
    const float maxX = (node.uMax - 0.5f) * m_terrainConfig.width;
    const float minZ = (node.vMin - 0.5f) * m_terrainConfig.length;
    const float maxZ = (node.vMax - 0.5f) * m_terrainConfig.length;

    float minY = std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    float maxDeviation = 0.0f;

    if (m_heightmap && m_heightmap->isValid()) {
        // Sample 5x5 grid inside the node to measure min/max elevation and mid-point geometric deviation
        constexpr uint32_t kSamples = 4u;
        const float du = (node.uMax - node.uMin) / static_cast<float>(kSamples);
        const float dv = (node.vMax - node.vMin) / static_cast<float>(kSamples);

        float heights[kSamples + 1][kSamples + 1]{};
        for (uint32_t iz = 0; iz <= kSamples; ++iz) {
            const float v = node.vMin + static_cast<float>(iz) * dv;
            for (uint32_t ix = 0; ix <= kSamples; ++ix) {
                const float u = node.uMin + static_cast<float>(ix) * du;
                const float h = m_terrainConfig.heightOffset + m_heightmap->sampleBilinear(u, v) * m_terrainConfig.heightScale;
                heights[iz][ix] = h;
                minY = std::min(minY, h);
                maxY = std::max(maxY, h);
            }
        }

        // Measure deviation of odd samples against linear interpolation of even samples
        for (uint32_t iz = 0; iz <= kSamples; ++iz) {
            for (uint32_t ix = 0; ix <= kSamples; ++ix) {
                const bool oddX = (ix & 1u) != 0u;
                const bool oddZ = (iz & 1u) != 0u;
                if (oddX && !oddZ) {
                    const float interp = 0.5f * (heights[iz][ix - 1] + heights[iz][ix + 1]);
                    maxDeviation = std::max(maxDeviation, std::abs(heights[iz][ix] - interp));
                } else if (!oddX && oddZ) {
                    const float interp = 0.5f * (heights[iz - 1][ix] + heights[iz + 1][ix]);
                    maxDeviation = std::max(maxDeviation, std::abs(heights[iz][ix] - interp));
                } else if (oddX && oddZ) {
                    const float interp = 0.5f * (heights[iz - 1][ix + 1] + heights[iz + 1][ix - 1]);
                    maxDeviation = std::max(maxDeviation, std::abs(heights[iz][ix] - interp));
                }
            }
        }
    } else {
        minY = m_terrainConfig.heightOffset;
        maxY = m_terrainConfig.heightOffset + m_terrainConfig.heightScale;
    }

    // Scale deviation from the 4x4 probe grid to the actual patchResolution grid (e.g. 16x16)
    const float resScale = 4.0f / static_cast<float>(std::max(m_lodConfig.patchResolution, 4u));
    const float patchWorldSize = std::max(maxX - minX, maxZ - minZ);
    const float baseError = patchWorldSize * 0.002f * resScale;
    node.geometricError = std::max(maxDeviation * resScale, baseError);

    node.minHeight = minY;
    node.maxHeight = maxY;
    node.worldBounds.min = glm::vec3(minX, minY, minZ);
    node.worldBounds.max = glm::vec3(maxX, maxY, maxZ);
}

bool TerrainQuadtree::shouldSubdivide(const TerrainQuadNode& node,
                                      const glm::vec3& cameraPos,
                                      float fovDegrees,
                                      uint32_t viewportHeight,
                                      float errorBiasMultiplier,
                                      bool currentlySplit) const noexcept {
    if (node.depth < m_lodConfig.minDepth) {
        return true;
    }
    if (node.depth >= m_lodConfig.maxDepth) {
        return false;
    }

    const float dist = distancePointToAABB(cameraPos, node.worldBounds);

    if (m_lodConfig.strategy == TerrainLODStrategy::DistanceBased) {
        const float patchSize = std::max(
            node.worldBounds.max.x - node.worldBounds.min.x,
            node.worldBounds.max.z - node.worldBounds.min.z
        );
        const float splitDist = patchSize * m_lodConfig.distanceSplitFactor / std::max(errorBiasMultiplier, 0.25f);
        const float threshold = currentlySplit ? (splitDist / m_lodConfig.lodHysteresis) : splitDist;
        return dist < threshold;
    }

    // Default: Screen-Space Geometric Error
    const float screenError = computeScreenSpaceError(node.geometricError, dist, fovDegrees, viewportHeight);
    const float baseThreshold = m_lodConfig.pixelErrorThreshold * std::max(errorBiasMultiplier, 0.25f);
    const float threshold = currentlySplit ? (baseThreshold * m_lodConfig.lodHysteresis) : baseThreshold;
    return screenError > threshold;
}

void TerrainQuadtree::subdivideNode(uint32_t nodeIndex, bool startMorphed) {
    if (nodeIndex >= m_nodes.size() || m_nodes[nodeIndex].hasChildren()) {
        return;
    }

    const uint32_t parentDepth = m_nodes[nodeIndex].depth;
    const uint32_t gx = m_nodes[nodeIndex].gridX * 2u;
    const uint32_t gz = m_nodes[nodeIndex].gridZ * 2u;
    const uint32_t childDepth = parentDepth + 1u;

    // Allocate 4 children (NW, NE, SW, SE) — note m_nodes may reallocate during emplace_back
    const uint32_t c0 = allocateNode(nodeIndex, childDepth, gx,      gz);
    const uint32_t c1 = allocateNode(nodeIndex, childDepth, gx + 1u, gz);
    const uint32_t c2 = allocateNode(nodeIndex, childDepth, gx,      gz + 1u);
    const uint32_t c3 = allocateNode(nodeIndex, childDepth, gx + 1u, gz + 1u);

    m_nodes[nodeIndex].childIndices = {c0, c1, c2, c3};
    m_nodes[nodeIndex].isLeaf = false;
    m_nodes[nodeIndex].patchState = TerrainPatchState::Inactive;

    const float initMorph = (startMorphed && m_lodConfig.geomorphingEnabled && m_lodConfig.morphDuration > 0.0f) ? 1.0f : 0.0f;
    for (uint32_t ci : m_nodes[nodeIndex].childIndices) {
        m_nodes[ci].morphFactor = initMorph;
        m_nodes[ci].targetMorphFactor = 0.0f;
        m_nodes[ci].patchState = (initMorph > 0.0f) ? TerrainPatchState::MorphingIn : TerrainPatchState::Active;
    }

    ++m_stats.lodTransitionCount;
}

void TerrainQuadtree::mergeNode(uint32_t nodeIndex) {
    if (nodeIndex >= m_nodes.size() || !m_nodes[nodeIndex].hasChildren()) {
        return;
    }

    // Recursively merge any grandchildren first
    const auto children = m_nodes[nodeIndex].childIndices;
    for (uint32_t ci : children) {
        if (ci != TerrainQuadNode::kInvalidIndex) {
            if (m_nodes[ci].hasChildren()) {
                mergeNode(ci);
            }
            m_keyToIndex.erase(m_nodes[ci].nodeId);
            m_nodes[ci].isLeaf = false;
            m_nodes[ci].patchState = TerrainPatchState::Inactive;
            m_freeNodeIndices.push_back(ci);
        }
    }

    m_nodes[nodeIndex].childIndices = {
        TerrainQuadNode::kInvalidIndex,
        TerrainQuadNode::kInvalidIndex,
        TerrainQuadNode::kInvalidIndex,
        TerrainQuadNode::kInvalidIndex
    };
    m_nodes[nodeIndex].isLeaf = true;
    m_nodes[nodeIndex].patchState = TerrainPatchState::Active;
    m_nodes[nodeIndex].morphFactor = 0.0f;
    m_nodes[nodeIndex].targetMorphFactor = 0.0f;
    ++m_stats.lodTransitionCount;
}

void TerrainQuadtree::evaluateRecursive(uint32_t nodeIndex,
                                        const glm::vec3& cameraPos,
                                        float fovDegrees,
                                        uint32_t viewportHeight,
                                        float errorBiasMultiplier,
                                        uint32_t& activeLeafBudget) {
    if (nodeIndex >= m_nodes.size()) {
        return;
    }

    const float dist = distancePointToAABB(cameraPos, m_nodes[nodeIndex].worldBounds);
    m_nodes[nodeIndex].screenSpaceError = computeScreenSpaceError(
        m_nodes[nodeIndex].geometricError, dist, fovDegrees, viewportHeight
    );

    const bool currentlySplit = m_nodes[nodeIndex].hasChildren();
    const bool wantSplit = shouldSubdivide(
        m_nodes[nodeIndex], cameraPos, fovDegrees, viewportHeight, errorBiasMultiplier, currentlySplit
    );

    if (wantSplit) {
        if (!currentlySplit && activeLeafBudget >= 3u) {
            subdivideNode(nodeIndex, true);
            activeLeafBudget -= 3u;
        }
        if (m_nodes[nodeIndex].hasChildren()) {
            const auto children = m_nodes[nodeIndex].childIndices;
            for (uint32_t ci : children) {
                evaluateRecursive(ci, cameraPos, fovDegrees, viewportHeight, errorBiasMultiplier, activeLeafBudget);
            }
        }
    } else if (currentlySplit) {
        mergeNode(nodeIndex);
        activeLeafBudget += 3u;
    }
}

uint32_t TerrainQuadtree::findLeafAt(uint32_t depth, int32_t gridX, int32_t gridZ) const noexcept {
    const int32_t maxCoord = static_cast<int32_t>(1u << depth);
    if (gridX < 0 || gridZ < 0 || gridX >= maxCoord || gridZ >= maxCoord) {
        return TerrainQuadNode::kInvalidIndex;
    }

    // Walk up from (depth, gridX, gridZ) until an existing leaf node is found
    uint32_t d = depth;
    uint32_t gx = static_cast<uint32_t>(gridX);
    uint32_t gz = static_cast<uint32_t>(gridZ);
    while (true) {
        const uint64_t key = makeTerrainNodeKey(d, gx, gz);
        auto it = m_keyToIndex.find(key);
        if (it != m_keyToIndex.end()) {
            const uint32_t idx = it->second;
            if (m_nodes[idx].isLeaf) {
                return idx;
            }
            // Node exists but is subdivided -> neighbor is finer than `depth`
            return idx;
        }
        if (d == 0u) {
            break;
        }
        --d;
        gx >>= 1u;
        gz >>= 1u;
    }
    return TerrainQuadNode::kInvalidIndex;
}

void TerrainQuadtree::balanceQuadtree() {
    if (!m_lodConfig.balanceQuadtree) {
        return;
    }

    // Iteratively subdivide any leaf node that has a neighbor at depth >= self.depth + 2
    bool changed = true;
    uint32_t safetyIterations = 0;
    while (changed && safetyIterations < 16u) {
        changed = false;
        ++safetyIterations;

        std::vector<uint32_t> toSubdivide;
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            const TerrainQuadNode& n = m_nodes[i];
            if (!n.isLeaf || n.depth >= m_lodConfig.maxDepth) {
                continue;
            }

            // Check 4 cardinal directions at depth = n.depth
            const int32_t gx = static_cast<int32_t>(n.gridX);
            const int32_t gz = static_cast<int32_t>(n.gridZ);
            const int32_t offsets[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};

            for (const auto& off : offsets) {
                const uint32_t nIdx = findLeafAt(n.depth, gx + off[0], gz + off[1]);
                if (nIdx == TerrainQuadNode::kInvalidIndex) {
                    continue;
                }
                // If neighbor at same depth is subdivided, check if any of its adjacent children are ALSO subdivided
                if (m_nodes[nIdx].hasChildren()) {
                    bool childIsSplit = false;
                    for (uint32_t ci : m_nodes[nIdx].childIndices) {
                        if (ci != TerrainQuadNode::kInvalidIndex && m_nodes[ci].hasChildren()) {
                            childIsSplit = true;
                            break;
                        }
                    }
                    if (childIsSplit) {
                        toSubdivide.push_back(static_cast<uint32_t>(i));
                        break;
                    }
                }
            }
        }

        for (uint32_t idx : toSubdivide) {
            if (m_nodes[idx].isLeaf) {
                subdivideNode(idx, false);
                changed = true;
            }
        }
    }
}

void TerrainQuadtree::updateNeighborsAndStitchMasks() {
    m_activeLeaves.clear();
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].isLeaf) {
            m_activeLeaves.push_back(static_cast<uint32_t>(i));
        }
    }

    for (uint32_t idx : m_activeLeaves) {
        TerrainQuadNode& n = m_nodes[idx];
        n.neighbors.reset();

        const int32_t gx = static_cast<int32_t>(n.gridX);
        const int32_t gz = static_cast<int32_t>(n.gridZ);

        auto queryDir = [&](int32_t dx, int32_t dz, uint64_t& outId, int32_t& outDepth, uint8_t edgeBit) {
            const uint32_t nIdx = findLeafAt(n.depth, gx + dx, gz + dz);
            if (nIdx != TerrainQuadNode::kInvalidIndex) {
                outId = m_nodes[nIdx].nodeId;
                outDepth = static_cast<int32_t>(m_nodes[nIdx].depth);
                if (m_lodConfig.stitchingEnabled && m_nodes[nIdx].depth < n.depth) {
                    n.neighbors.stitchMask |= edgeBit;
                }
            }
        };

        queryDir(0, -1, n.neighbors.northNodeId, n.neighbors.northDepth, kEdgeNorth);
        queryDir(0,  1, n.neighbors.southNodeId, n.neighbors.southDepth, kEdgeSouth);
        queryDir(-1, 0, n.neighbors.westNodeId,  n.neighbors.westDepth,  kEdgeWest);
        queryDir( 1, 0, n.neighbors.eastNodeId,  n.neighbors.eastDepth,  kEdgeEast);
    }
}

void TerrainQuadtree::updateGeomorphing(float deltaTime) {
    if (!m_lodConfig.geomorphingEnabled || m_lodConfig.morphDuration <= 0.0001f) {
        for (uint32_t idx : m_activeLeaves) {
            m_nodes[idx].morphFactor = 0.0f;
            m_nodes[idx].patchState = TerrainPatchState::Active;
        }
        return;
    }

    const float step = std::clamp(deltaTime / m_lodConfig.morphDuration, 0.0f, 1.0f);
    for (uint32_t idx : m_activeLeaves) {
        TerrainQuadNode& n = m_nodes[idx];
        if (n.morphFactor > n.targetMorphFactor) {
            n.morphFactor = std::max(n.targetMorphFactor, n.morphFactor - step);
            if (n.morphFactor <= 0.0001f) {
                n.morphFactor = 0.0f;
                n.patchState = TerrainPatchState::Active;
            }
        } else if (n.morphFactor < n.targetMorphFactor) {
            n.morphFactor = std::min(n.targetMorphFactor, n.morphFactor + step);
        }
    }
}

void TerrainQuadtree::updateVisibilityAndStats(const Frustum& frustum, bool frustumCull) {
    m_stats.totalQuadtreeNodes = static_cast<uint32_t>(m_nodes.size() - m_freeNodeIndices.size());
    m_stats.activeLeafNodes = static_cast<uint32_t>(m_activeLeaves.size());
    m_stats.visibleTerrainPatches = 0;
    m_stats.culledTerrainPatches = 0;
    m_stats.patchCountPerLod.fill(0);
    m_stats.stitchingPatchCount = 0;
    m_stats.maxScreenSpaceError = 0.0f;

    double sumError = 0.0;
    double sumMorph = 0.0;
    uint32_t visCount = 0;

    for (uint32_t idx : m_activeLeaves) {
        TerrainQuadNode& n = m_nodes[idx];
        n.isVisible = frustumCull ? frustum.intersectsAABB(n.worldBounds) : true;
        if (n.isVisible) {
            ++m_stats.visibleTerrainPatches;
            ++visCount;
            const uint32_t lodBucket = std::min(n.lodLevel, TerrainLODStatistics::kMaxTrackedLods - 1u);
            ++m_stats.patchCountPerLod[lodBucket];
            if (n.neighbors.needsStitching()) {
                ++m_stats.stitchingPatchCount;
            }
            m_stats.maxScreenSpaceError = std::max(m_stats.maxScreenSpaceError, n.screenSpaceError);
            sumError += static_cast<double>(n.screenSpaceError);
            sumMorph += static_cast<double>(n.morphFactor);
        } else {
            ++m_stats.culledTerrainPatches;
        }
    }

    if (visCount > 0u) {
        m_stats.averageScreenSpaceError = static_cast<float>(sumError / static_cast<double>(visCount));
        m_stats.averageMorphFactor = static_cast<float>(sumMorph / static_cast<double>(visCount));
    } else {
        m_stats.averageScreenSpaceError = 0.0f;
        m_stats.averageMorphFactor = 0.0f;
    }
}

void TerrainQuadtree::update(float deltaTime,
                             const Camera& camera,
                             uint32_t viewportHeight,
                             bool frustumCull,
                             float errorBiasMultiplier) {
    if (!m_initialized || m_nodes.empty()) {
        return;
    }

    const auto t0 = std::chrono::high_resolution_clock::now();
    m_stats.lodTransitionCount = 0;

    uint32_t leafBudget = m_lodConfig.maxActivePatches > 1u ? (m_lodConfig.maxActivePatches - 1u) : 1u;
    evaluateRecursive(0u, camera.getPosition(), camera.getFovDegrees(), viewportHeight, errorBiasMultiplier, leafBudget);
    balanceQuadtree();
    updateNeighborsAndStitchMasks();
    updateGeomorphing(deltaTime);

    const glm::mat4 vp = camera.getProjectionMatrix() * camera.getViewMatrix();
    const Frustum frustum = Frustum::fromViewProj(vp);
    updateVisibilityAndStats(frustum, frustumCull);

    const auto t1 = std::chrono::high_resolution_clock::now();
    m_stats.quadtreeUpdateTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
}

TerrainLODSelection TerrainQuadtree::buildSelection(uint32_t patchResolution) const {
    const auto t0 = std::chrono::high_resolution_clock::now();
    TerrainLODSelection sel{};
    sel.stats = m_stats;
    sel.allLeafPatches.reserve(m_activeLeaves.size());
    sel.visiblePatches.reserve(m_stats.visibleTerrainPatches);

    const uint32_t R = (patchResolution < 4u) ? 4u : ((patchResolution & 1u) ? patchResolution + 1u : patchResolution);
    const uint64_t vertsPerPatch = static_cast<uint64_t>(R + 1u) * (R + 1u);

    uint64_t totalVerts = 0;
    uint64_t totalTris = 0;

    for (uint32_t idx : m_activeLeaves) {
        const TerrainQuadNode& n = m_nodes[idx];
        TerrainPatchSelectionEntry entry{};
        entry.nodeId = n.nodeId;
        entry.depth = n.depth;
        entry.lodLevel = n.lodLevel;
        entry.gridX = n.gridX;
        entry.gridZ = n.gridZ;
        entry.uMin = n.uMin;
        entry.uMax = n.uMax;
        entry.vMin = n.vMin;
        entry.vMax = n.vMax;
        entry.worldBounds = n.worldBounds;
        entry.geometricError = n.geometricError;
        entry.screenSpaceError = n.screenSpaceError;
        entry.morphFactor = n.morphFactor;
        entry.stitchMask = n.neighbors.stitchMask;
        entry.isVisible = n.isVisible;

        sel.allLeafPatches.push_back(entry);
        if (n.isVisible) {
            sel.visiblePatches.push_back(entry);
            totalVerts += vertsPerPatch;
            // Exact triangle count from stitch pattern
            const uint32_t idxCount = static_cast<uint32_t>(
                TerrainStitchIndexPool::generateStitchIndices(R, entry.stitchMask).size()
            );
            totalTris += static_cast<uint64_t>(idxCount / 3u);
        }
    }

    sel.stats.totalTerrainVertices = totalVerts;
    sel.stats.totalTerrainTriangles = totalTris;

    const auto t1 = std::chrono::high_resolution_clock::now();
    sel.stats.patchSelectionTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    return sel;
}

bool TerrainQuadtree::validateNeighborBalance() const noexcept {
    for (uint32_t idx : m_activeLeaves) {
        const TerrainQuadNode& n = m_nodes[idx];
        if (n.neighbors.maxDepthDifference(static_cast<int32_t>(n.depth)) > 1) {
            return false;
        }
    }
    return true;
}

std::vector<TerrainDebugBox> TerrainQuadtree::getDebugBoxes() const {
    static constexpr glm::vec4 kLodColors[5] = {
        glm::vec4(0.95f, 0.20f, 0.20f, 1.0f), // LOD 0 (Finest): Red
        glm::vec4(1.00f, 0.55f, 0.10f, 1.0f), // LOD 1: Orange
        glm::vec4(0.95f, 0.90f, 0.15f, 1.0f), // LOD 2: Yellow
        glm::vec4(0.20f, 0.85f, 0.30f, 1.0f), // LOD 3: Green
        glm::vec4(0.20f, 0.55f, 0.98f, 1.0f)  // LOD 4+ (Coarsest): Blue
    };

    std::vector<TerrainDebugBox> boxes;
    boxes.reserve(m_activeLeaves.size());
    for (uint32_t idx : m_activeLeaves) {
        const TerrainQuadNode& n = m_nodes[idx];
        if (!n.isVisible) continue;
        const uint32_t colorIdx = std::min(n.lodLevel, 4u);
        boxes.push_back(TerrainDebugBox{
            .bounds = n.worldBounds,
            .color = kLodColors[colorIdx],
            .lodLevel = n.lodLevel
        });
    }
    return boxes;
}

} // namespace sidera
