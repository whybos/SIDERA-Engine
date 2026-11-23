#pragma once

#include <cstdint>

namespace sidera {

// Edge bit flags for 4-bit stitching pattern mask (0..15)
enum TerrainEdgeBit : uint8_t {
    kEdgeNone  = 0,
    kEdgeNorth = 1 << 0, // -Z edge (iz == 0)
    kEdgeSouth = 1 << 1, // +Z edge (iz == R)
    kEdgeWest  = 1 << 2, // -X edge (ix == 0)
    kEdgeEast  = 1 << 3  // +X edge (ix == R)
};

// Neighbor relationship metadata for a TerrainQuadNode
struct TerrainNeighborData {
    static constexpr uint64_t kInvalidNodeId = 0xFFFFFFFFFFFFFFFFULL;

    uint64_t northNodeId = kInvalidNodeId; // -Z neighbor
    uint64_t southNodeId = kInvalidNodeId; // +Z neighbor
    uint64_t westNodeId  = kInvalidNodeId; // -X neighbor
    uint64_t eastNodeId  = kInvalidNodeId; // +X neighbor

    // Neighbor quadtree depths (-1 if outside terrain boundary)
    int32_t northDepth = -1;
    int32_t southDepth = -1;
    int32_t westDepth  = -1;
    int32_t eastDepth  = -1;

    // 4-bit stitch mask: bit set when the corresponding neighbor is strictly coarser (depth < self.depth)
    uint8_t stitchMask = kEdgeNone;

    [[nodiscard]] bool needsStitching() const noexcept {
        return stitchMask != kEdgeNone;
    }

    [[nodiscard]] int32_t maxDepthDifference(int32_t selfDepth) const noexcept {
        int32_t maxDiff = 0;
        auto check = [&](int32_t nd) {
            if (nd >= 0) {
                const int32_t diff = selfDepth > nd ? (selfDepth - nd) : (nd - selfDepth);
                if (diff > maxDiff) maxDiff = diff;
            }
        };
        check(northDepth);
        check(southDepth);
        check(westDepth);
        check(eastDepth);
        return maxDiff;
    }

    void reset() noexcept {
        northNodeId = kInvalidNodeId;
        southNodeId = kInvalidNodeId;
        westNodeId  = kInvalidNodeId;
        eastNodeId  = kInvalidNodeId;
        northDepth = -1;
        southDepth = -1;
        westDepth  = -1;
        eastDepth  = -1;
        stitchMask = kEdgeNone;
    }
};

} // namespace sidera
