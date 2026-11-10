#include "lod/MeshSimplifier.h"
#include "core/Log.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace sidera {

namespace {

// Symmetric 4x4 Quadric Error Matrix stored as 10 unique elements:
// [ 0  1  2  3 ]
// [ 1  4  5  6 ]
// [ 2  5  7  8 ]
// [ 3  6  8  9 ]
struct SymmMatrix4 {
    float m[10]{};

    SymmMatrix4() = default;

    SymmMatrix4(float a, float b, float c, float d) {
        m[0] = a * a;  m[1] = a * b;  m[2] = a * c;  m[3] = a * d;
                       m[4] = b * b;  m[5] = b * c;  m[6] = b * d;
                                      m[7] = c * c;  m[8] = c * d;
                                                     m[9] = d * d;
    }

    SymmMatrix4& operator+=(const SymmMatrix4& o) noexcept {
        for (int i = 0; i < 10; ++i) {
            m[i] += o.m[i];
        }
        return *this;
    }

    SymmMatrix4 operator+(const SymmMatrix4& o) const noexcept {
        SymmMatrix4 r = *this;
        r += o;
        return r;
    }

    SymmMatrix4 operator*(float s) const noexcept {
        SymmMatrix4 r;
        for (int i = 0; i < 10; ++i) {
            r.m[i] = m[i] * s;
        }
        return r;
    }

    // Evaluates quadric error at 3D point p = (x, y, z)
    [[nodiscard]] float evaluate(const glm::vec3& p) const noexcept {
        const float x = p.x;
        const float y = p.y;
        const float z = p.z;
        return m[0] * x * x + 2.0f * m[1] * x * y + 2.0f * m[2] * x * z + 2.0f * m[3] * x
             + m[4] * y * y + 2.0f * m[5] * y * z + 2.0f * m[6] * y
             + m[7] * z * z + 2.0f * m[8] * z
             + m[9];
    }
};

struct EdgeCollapseCandidate {
    uint32_t u = 0;
    uint32_t v = 0;
    float cost = 0.0f;
    glm::vec3 optimalPos{0.0f};

    bool operator>(const EdgeCollapseCandidate& o) const noexcept {
        return cost > o.cost;
    }
};

// Computes normal of triangle formed by 3 points
glm::vec3 computeTriangleNormal(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2) {
    const glm::vec3 e1 = p1 - p0;
    const glm::vec3 e2 = p2 - p0;
    const glm::vec3 cross = glm::cross(e1, e2);
    const float len = glm::length(cross);
    if (len < 1e-8f) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return cross / len;
}

} // namespace

SimplificationResult MeshSimplifier::simplify(
    const std::vector<Vertex>& inVertices,
    const std::vector<uint32_t>& inIndices,
    const SimplificationOptions& options,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices
) {
    SimplificationResult res{};
    res.originalVertices = static_cast<uint32_t>(inVertices.size());
    res.originalTriangles = static_cast<uint32_t>(inIndices.size() / 3);

    if (inVertices.empty() || inIndices.size() < 3) {
        outVertices = inVertices;
        outIndices = inIndices;
        res.simplifiedVertices = res.originalVertices;
        res.simplifiedTriangles = res.originalTriangles;
        res.success = false;
        return res;
    }

    uint32_t targetTriangles = options.targetTriangles;
    if (targetTriangles == 0) {
        targetTriangles = static_cast<uint32_t>(std::ceil(static_cast<float>(res.originalTriangles) * options.targetRatio));
    }
    targetTriangles = std::clamp(targetTriangles, 1u, res.originalTriangles);

    if (targetTriangles >= res.originalTriangles) {
        outVertices = inVertices;
        outIndices = inIndices;
        res.simplifiedVertices = res.originalVertices;
        res.simplifiedTriangles = res.originalTriangles;
        res.achievedRatio = 1.0f;
        res.success = true;
        return res;
    }

    // Working copy of vertices
    struct SimpVertex {
        Vertex v;
        SymmMatrix4 quadric{};
        bool deleted = false;
        uint32_t remappedTo = 0;
    };

    std::vector<SimpVertex> vertices(inVertices.size());
    for (size_t i = 0; i < inVertices.size(); ++i) {
        vertices[i].v = inVertices[i];
        vertices[i].remappedTo = static_cast<uint32_t>(i);
    }

    // Triangles
    struct SimpTriangle {
        uint32_t v[3];
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        bool deleted = false;
    };

    const size_t numTriangles = inIndices.size() / 3;
    std::vector<SimpTriangle> triangles(numTriangles);

    // Vertex to incident triangle mapping
    std::vector<std::vector<uint32_t>> vertexTriangles(inVertices.size());

    // Step 1: Calculate initial vertex quadrics from incident triangle planes
    for (size_t t = 0; t < numTriangles; ++t) {
        const uint32_t i0 = inIndices[t * 3 + 0];
        const uint32_t i1 = inIndices[t * 3 + 1];
        const uint32_t i2 = inIndices[t * 3 + 2];

        triangles[t].v[0] = i0;
        triangles[t].v[1] = i1;
        triangles[t].v[2] = i2;

        vertexTriangles[i0].push_back(static_cast<uint32_t>(t));
        vertexTriangles[i1].push_back(static_cast<uint32_t>(t));
        vertexTriangles[i2].push_back(static_cast<uint32_t>(t));

        const glm::vec3& p0 = vertices[i0].v.position;
        const glm::vec3& p1 = vertices[i1].v.position;
        const glm::vec3& p2 = vertices[i2].v.position;

        const glm::vec3 e1 = p1 - p0;
        const glm::vec3 e2 = p2 - p0;
        const glm::vec3 cross = glm::cross(e1, e2);
        const float area2 = glm::length(cross);

        if (area2 > 1e-12f) {
            const glm::vec3 normal = cross / area2;
            triangles[t].normal = normal;
            const float d = -glm::dot(normal, p0);
            const SymmMatrix4 qPlane(normal.x, normal.y, normal.z, d);
            const SymmMatrix4 qWeighted = qPlane * (area2 * 0.5f);

            vertices[i0].quadric += qWeighted;
            vertices[i1].quadric += qWeighted;
            vertices[i2].quadric += qWeighted;
        }
    }

    // Step 2: Boundary edge detection and penalty quadrics
    if (options.preserveBorder) {
        std::map<std::pair<uint32_t, uint32_t>, int> edgeCounts;
        for (size_t t = 0; t < numTriangles; ++t) {
            const uint32_t v0 = triangles[t].v[0];
            const uint32_t v1 = triangles[t].v[1];
            const uint32_t v2 = triangles[t].v[2];

            edgeCounts[{std::min(v0, v1), std::max(v0, v1)}]++;
            edgeCounts[{std::min(v1, v2), std::max(v1, v2)}]++;
            edgeCounts[{std::min(v2, v0), std::max(v2, v0)}]++;
        }

        for (const auto& [edge, count] : edgeCounts) {
            if (count == 1) { // Border edge
                const uint32_t u = edge.first;
                const uint32_t v = edge.second;
                const glm::vec3 edgeVec = vertices[v].v.position - vertices[u].v.position;
                const float edgeLen = glm::length(edgeVec);
                if (edgeLen > 1e-6f) {
                    // Approximate border plane normal
                    glm::vec3 avgNormal = vertices[u].v.normal;
                    glm::vec3 borderNormal = glm::cross(edgeVec / edgeLen, avgNormal);
                    const float bLen = glm::length(borderNormal);
                    if (bLen > 1e-6f) {
                        borderNormal /= bLen;
                        const float d = -glm::dot(borderNormal, vertices[u].v.position);
                        const SymmMatrix4 bQuad = SymmMatrix4(borderNormal.x, borderNormal.y, borderNormal.z, d) * options.borderWeight;
                        vertices[u].quadric += bQuad;
                        vertices[v].quadric += bQuad;
                    }
                }
            }
        }
    }

    // Step 3: Helper to calculate optimal position and error for collapsing edge (u, v)
    auto evaluateEdge = [&](uint32_t u, uint32_t v, glm::vec3& outPos) -> float {
        const SymmMatrix4 Q = vertices[u].quadric + vertices[v].quadric;
        const glm::vec3 pU = vertices[u].v.position;
        const glm::vec3 pV = vertices[v].v.position;
        const glm::vec3 pMid = (pU + pV) * 0.5f;

        const float costU = Q.evaluate(pU);
        const float costV = Q.evaluate(pV);
        const float costMid = Q.evaluate(pMid);

        float minCost = costMid;
        outPos = pMid;

        if (costU < minCost) {
            minCost = costU;
            outPos = pU;
        }
        if (costV < minCost) {
            minCost = costV;
            outPos = pV;
        }

        return std::max(0.0f, minCost);
    };

    // Helper to test if collapsing (u, v) to targetPos would flip any incident triangle
    auto checkFlipping = [&](uint32_t u, uint32_t v, const glm::vec3& targetPos) -> bool {
        for (uint32_t tIdx : vertexTriangles[u]) {
            const auto& t = triangles[tIdx];
            if (t.deleted) continue;
            if (t.v[0] == v || t.v[1] == v || t.v[2] == v) continue; // Will be deleted

            glm::vec3 p0 = (t.v[0] == u) ? targetPos : vertices[t.v[0]].v.position;
            glm::vec3 p1 = (t.v[1] == u) ? targetPos : vertices[t.v[1]].v.position;
            glm::vec3 p2 = (t.v[2] == u) ? targetPos : vertices[t.v[2]].v.position;

            const glm::vec3 newNormal = computeTriangleNormal(p0, p1, p2);
            if (glm::dot(newNormal, t.normal) < 0.2f) {
                return true; // Flipped!
            }
        }
        for (uint32_t tIdx : vertexTriangles[v]) {
            const auto& t = triangles[tIdx];
            if (t.deleted) continue;
            if (t.v[0] == u || t.v[1] == u || t.v[2] == u) continue;

            glm::vec3 p0 = (t.v[0] == v) ? targetPos : vertices[t.v[0]].v.position;
            glm::vec3 p1 = (t.v[1] == v) ? targetPos : vertices[t.v[1]].v.position;
            glm::vec3 p2 = (t.v[2] == v) ? targetPos : vertices[t.v[2]].v.position;

            const glm::vec3 newNormal = computeTriangleNormal(p0, p1, p2);
            if (glm::dot(newNormal, t.normal) < 0.2f) {
                return true; // Flipped!
            }
        }
        return false;
    };

    // Priority queue of collapse candidates
    std::priority_queue<EdgeCollapseCandidate, std::vector<EdgeCollapseCandidate>, std::greater<EdgeCollapseCandidate>> queue;

    // Populate initial edges
    std::unordered_set<uint64_t> visitedEdges;
    auto makeEdgeKey = [](uint32_t a, uint32_t b) -> uint64_t {
        return (static_cast<uint64_t>(std::min(a, b)) << 32) | static_cast<uint64_t>(std::max(a, b));
    };

    for (size_t t = 0; t < numTriangles; ++t) {
        const uint32_t v0 = triangles[t].v[0];
        const uint32_t v1 = triangles[t].v[1];
        const uint32_t v2 = triangles[t].v[2];

        const std::pair<uint32_t, uint32_t> edges[3] = {{v0, v1}, {v1, v2}, {v2, v0}};
        for (const auto& e : edges) {
            const uint64_t key = makeEdgeKey(e.first, e.second);
            if (visitedEdges.insert(key).second) {
                glm::vec3 optPos;
                const float cost = evaluateEdge(e.first, e.second, optPos);
                queue.push({e.first, e.second, cost, optPos});
            }
        }
    }

    uint32_t currentTriangles = static_cast<uint32_t>(numTriangles);
    float maxObservedError = 0.0f;

    // Step 4: Iteratively collapse edges with lowest quadric error
    while (currentTriangles > targetTriangles && !queue.empty()) {
        const auto top = queue.top();
        queue.pop();

        const uint32_t u = top.u;
        const uint32_t v = top.v;

        if (vertices[u].deleted || vertices[v].deleted) {
            continue;
        }

        // Check if error exceeds limit
        if (top.cost > options.maxError) {
            break;
        }

        // Re-evaluate cost to verify it hasn't changed drastically
        glm::vec3 currentOptPos;
        const float currentCost = evaluateEdge(u, v, currentOptPos);
        if (std::abs(currentCost - top.cost) > 1e-3f * (1.0f + top.cost)) {
            queue.push({u, v, currentCost, currentOptPos});
            continue;
        }

        // Check face flipping
        if (options.checkFaceFlipping && checkFlipping(u, v, currentOptPos)) {
            continue;
        }

        // Perform collapse: merge v into u at currentOptPos
        maxObservedError = std::max(maxObservedError, currentCost);

        // Interpolate vertex attributes
        const glm::vec3 pU = vertices[u].v.position;
        const glm::vec3 pV = vertices[v].v.position;
        const float edgeLenSq = glm::dot(pV - pU, pV - pU);
        float alpha = 0.5f;
        if (edgeLenSq > 1e-8f) {
            alpha = std::clamp(glm::dot(currentOptPos - pU, pV - pU) / edgeLenSq, 0.0f, 1.0f);
        }

        vertices[u].v.position = currentOptPos;
        vertices[u].v.uv = glm::mix(vertices[u].v.uv, vertices[v].v.uv, alpha);
        vertices[u].v.color = glm::mix(vertices[u].v.color, vertices[v].v.color, alpha);
        const glm::vec3 blendedNormal = glm::mix(vertices[u].v.normal, vertices[v].v.normal, alpha);
        const float nLen = glm::length(blendedNormal);
        vertices[u].v.normal = (nLen > 1e-6f) ? (blendedNormal / nLen) : glm::vec3(0.0f, 1.0f, 0.0f);

        vertices[u].quadric += vertices[v].quadric;
        vertices[v].deleted = true;
        vertices[v].remappedTo = u;

        // Update incident triangles
        std::vector<uint32_t> newIncident;
        newIncident.reserve(vertexTriangles[u].size() + vertexTriangles[v].size());

        auto processTri = [&](uint32_t tIdx) {
            auto& t = triangles[tIdx];
            if (t.deleted) return;

            // Replace v with u
            for (int k = 0; k < 3; ++k) {
                if (t.v[k] == v) {
                    t.v[k] = u;
                }
            }

            // Check if triangle became degenerate
            if (t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[2] == t.v[0]) {
                t.deleted = true;
                if (currentTriangles > 0) {
                    --currentTriangles;
                }
            } else {
                t.normal = computeTriangleNormal(
                    vertices[t.v[0]].v.position,
                    vertices[t.v[1]].v.position,
                    vertices[t.v[2]].v.position
                );
                newIncident.push_back(tIdx);
            }
        };

        for (uint32_t tIdx : vertexTriangles[u]) {
            processTri(tIdx);
        }
        for (uint32_t tIdx : vertexTriangles[v]) {
            processTri(tIdx);
        }

        vertexTriangles[u] = std::move(newIncident);
        vertexTriangles[v].clear();

        // Push updated edges incident to u
        std::unordered_set<uint32_t> neighborVertices;
        for (uint32_t tIdx : vertexTriangles[u]) {
            const auto& t = triangles[tIdx];
            if (t.deleted) continue;
            for (int k = 0; k < 3; ++k) {
                if (t.v[k] != u && !vertices[t.v[k]].deleted) {
                    neighborVertices.insert(t.v[k]);
                }
            }
        }

        for (uint32_t nbr : neighborVertices) {
            glm::vec3 optP;
            const float newCost = evaluateEdge(u, nbr, optP);
            queue.push({u, nbr, newCost, optP});
        }
    }

    // Step 5: Compact output vertices and rebuild index buffer
    outVertices.clear();
    outIndices.clear();

    std::vector<int32_t> oldToNew(vertices.size(), -1);

    for (const auto& t : triangles) {
        if (t.deleted) continue;

        for (int k = 0; k < 3; ++k) {
            const uint32_t oldIdx = t.v[k];
            if (oldToNew[oldIdx] == -1) {
                oldToNew[oldIdx] = static_cast<int32_t>(outVertices.size());
                outVertices.push_back(vertices[oldIdx].v);
            }
            outIndices.push_back(static_cast<uint32_t>(oldToNew[oldIdx]));
        }
    }

    res.simplifiedVertices = static_cast<uint32_t>(outVertices.size());
    res.simplifiedTriangles = static_cast<uint32_t>(outIndices.size() / 3);
    res.achievedRatio = static_cast<float>(res.simplifiedTriangles) / static_cast<float>(res.originalTriangles);
    res.maxQuadricError = maxObservedError;
    res.success = true;

    SIDERA_LOG_INFO(
        "LOD",
        "Mesh decimation complete: {} -> {} tris ({:.1f}% reduction), {} -> {} verts, max error: {:.5f}.",
        res.originalTriangles,
        res.simplifiedTriangles,
        (1.0f - res.achievedRatio) * 100.0f,
        res.originalVertices,
        res.simplifiedVertices,
        res.maxQuadricError
    );

    return res;
}

} // namespace sidera
