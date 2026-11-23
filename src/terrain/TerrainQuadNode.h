#pragma once

#include "renderer/BoundingVolume.h"
#include "terrain/TerrainNeighborData.h"

#include <glm/vec4.hpp>

#include <array>
#include <cstdint>

namespace sidera {

enum class TerrainPatchState : uint8_t {
    Inactive = 0,
    Stable,
    PreparingSplit,
    Splitting,     // Newly subdivided child group morphing from parent surface (1 -> 0)
    StableFine,    // Fully morphed to fine detail (morphFactor == 0)
    PreparingMerge,
    Merging        // Child group morphing back toward parent surface (0 -> 1) before collapse
};

// Deterministic 64-bit node key encoding (depth, gridX, gridZ)
[[nodiscard]] constexpr uint64_t makeTerrainNodeKey(uint32_t depth, uint32_t gridX, uint32_t gridZ) noexcept {
    return (static_cast<uint64_t>(depth & 0xFFu) << 56) |
           (static_cast<uint64_t>(gridX & 0x0FFFFFFFu) << 28) |
           static_cast<uint64_t>(gridZ & 0x0FFFFFFFu);
}

struct TerrainQuadNode {
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

    uint64_t nodeId = 0;
    uint32_t parentIndex = kInvalidIndex;
    std::array<uint32_t, 4> childIndices = {kInvalidIndex, kInvalidIndex, kInvalidIndex, kInvalidIndex}; // 0:NW, 1:NE, 2:SW, 3:SE

    uint32_t gridX = 0;
    uint32_t gridZ = 0;
    uint32_t depth = 0;
    uint32_t lodLevel = 0; // 0 = finest (maxDepth), increasing = coarser

    // Normalized terrain UV domain [u0, u1] x [v0, v1]
    float uMin = 0.0f;
    float uMax = 1.0f;
    float vMin = 0.0f;
    float vMax = 1.0f;

    AABB worldBounds{};
    float minHeight = 0.0f;
    float maxHeight = 0.0f;

    // World-space maximum geometric error against full-resolution heightmap
    float geometricError = 0.0f;
    // Last evaluated screen-space error in pixels
    float screenSpaceError = 0.0f;

    // Temporal geomorphing state: 0.0 = full child detail, 1.0 = morphed to parent surface
    float morphFactor = 0.0f;
    float targetMorphFactor = 0.0f;
    float transitionProgress = 1.0f;  // linear [0..1] progress along current transition
    float transitionElapsed = 0.0f;
    float transitionDuration = 0.35f;

    // Synchronized per-edge morph factors (x=North, y=South, z=West, w=East)
    glm::vec4 edgeMorph{0.0f, 0.0f, 0.0f, 0.0f};

    TerrainNeighborData neighbors{};
    bool isVisible = true;
    bool isLeaf = true;
    TerrainPatchState patchState = TerrainPatchState::Inactive;

    // World Partition / World Streaming association
    uint64_t partitionCellId = 0;
    uint32_t streamingRegionId = 0;

    [[nodiscard]] bool hasChildren() const noexcept {
        return childIndices[0] != kInvalidIndex;
    }
};

} // namespace sidera
