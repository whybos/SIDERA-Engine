#pragma once

#include "renderer/Vertex.h"

#include <cstdint>
#include <vector>

namespace sidera {

struct SimplificationOptions {
    float targetRatio = 0.5f;          // Target triangle count ratio (0.5 = 50% triangles)
    uint32_t targetTriangles = 0;       // Explicit target triangle count (0 = use targetRatio)
    float maxError = 1e6f;              // Maximum allowable quadric error before terminating decimation
    bool preserveBorder = true;         // Penalize border edge collapse to prevent boundary holes
    float borderWeight = 500.0f;        // Quadric penalty weight for boundary edges
    bool checkFaceFlipping = true;      // Prevent edge collapse that inverts triangle normals
};

struct SimplificationResult {
    uint32_t originalVertices = 0;
    uint32_t originalTriangles = 0;
    uint32_t simplifiedVertices = 0;
    uint32_t simplifiedTriangles = 0;
    float achievedRatio = 1.0f;
    float maxQuadricError = 0.0f;
    bool success = false;
};

// Quadric Error Metrics (QEM) based CPU mesh decimation and simplification
class MeshSimplifier {
public:
    // Decimates input mesh vertices and indices to produce a reduced geometric LOD
    [[nodiscard]] static SimplificationResult simplify(
        const std::vector<Vertex>& inVertices,
        const std::vector<uint32_t>& inIndices,
        const SimplificationOptions& options,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices
    );
};

} // namespace sidera
