#include "renderer/ProceduralMeshes.h"
#include "core/Log.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace sidera {

namespace {

constexpr float kPi = glm::pi<float>();
constexpr float kTwoPi = glm::two_pi<float>();

// Compute face normal from 3 triangle vertices
glm::vec3 computeFaceNormal(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2) {
    const glm::vec3 e1 = p1 - p0;
    const glm::vec3 e2 = p2 - p0;
    const glm::vec3 n = glm::cross(e1, e2);
    const float len = glm::length(n);
    return len > 1e-6f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace

// ============================================================================
// A. High-Resolution Icosphere
// ============================================================================
std::unique_ptr<Mesh> ProceduralMeshes::createIcosphere(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    float radius,
    uint32_t subdivisions,
    std::string_view name
) {
    // 12 base vertices of an icosahedron
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;

    std::vector<glm::vec3> positions = {
        glm::normalize(glm::vec3(-1.0f,  t,  0.0f)),
        glm::normalize(glm::vec3( 1.0f,  t,  0.0f)),
        glm::normalize(glm::vec3(-1.0f, -t,  0.0f)),
        glm::normalize(glm::vec3( 1.0f, -t,  0.0f)),

        glm::normalize(glm::vec3( 0.0f, -1.0f,  t)),
        glm::normalize(glm::vec3( 0.0f,  1.0f,  t)),
        glm::normalize(glm::vec3( 0.0f, -1.0f, -t)),
        glm::normalize(glm::vec3( 0.0f,  1.0f, -t)),

        glm::normalize(glm::vec3( t,  0.0f, -1.0f)),
        glm::normalize(glm::vec3( t,  0.0f,  1.0f)),
        glm::normalize(glm::vec3(-t,  0.0f, -1.0f)),
        glm::normalize(glm::vec3(-t,  0.0f,  1.0f))
    };

    // 20 initial triangular faces
    std::vector<uint32_t> indices = {
        0, 11, 5,   0, 5, 1,    0, 1, 7,    0, 7, 10,   0, 10, 11,
        1, 5, 9,    5, 11, 4,   11, 10, 2,  10, 7, 6,   7, 1, 8,
        3, 9, 4,    3, 4, 2,    3, 2, 6,    3, 6, 8,    3, 8, 9,
        4, 9, 5,    2, 4, 11,   6, 2, 10,   8, 6, 7,    9, 8, 1
    };

    // Subdivide recursively using midpoint deduplication
    std::map<uint64_t, uint32_t> midpointCache;
    auto getMidpoint = [&](uint32_t i1, uint32_t i2) -> uint32_t {
        const uint32_t smaller = std::min(i1, i2);
        const uint32_t greater = std::max(i1, i2);
        const uint64_t key = (static_cast<uint64_t>(smaller) << 32) | static_cast<uint64_t>(greater);

        const auto it = midpointCache.find(key);
        if (it != midpointCache.end()) {
            return it->second;
        }

        const glm::vec3 mid = glm::normalize((positions[i1] + positions[i2]) * 0.5f);
        const uint32_t index = static_cast<uint32_t>(positions.size());
        positions.push_back(mid);
        midpointCache[key] = index;
        return index;
    };

    for (uint32_t s = 0; s < subdivisions; ++s) {
        std::vector<uint32_t> newIndices;
        newIndices.reserve(indices.size() * 4);
        midpointCache.clear();

        for (size_t i = 0; i < indices.size(); i += 3) {
            const uint32_t v0 = indices[i + 0];
            const uint32_t v1 = indices[i + 1];
            const uint32_t v2 = indices[i + 2];

            const uint32_t a = getMidpoint(v0, v1);
            const uint32_t b = getMidpoint(v1, v2);
            const uint32_t c = getMidpoint(v2, v0);

            newIndices.push_back(v0); newIndices.push_back(a);  newIndices.push_back(c);
            newIndices.push_back(v1); newIndices.push_back(b);  newIndices.push_back(a);
            newIndices.push_back(v2); newIndices.push_back(c);  newIndices.push_back(b);
            newIndices.push_back(a);  newIndices.push_back(b);  newIndices.push_back(c);
        }
        indices = std::move(newIndices);
    }

    // Build final vertices with radius scaling, smooth spherical normals, and spherical UVs
    std::vector<Vertex> vertices;
    vertices.reserve(positions.size());

    for (const auto& unitPos : positions) {
        const glm::vec3 pos = unitPos * radius;
        const glm::vec3 norm = unitPos; // On unit sphere, position == normal

        const float u = 0.5f + std::atan2(norm.z, norm.x) / kTwoPi;
        const float v = 0.5f - std::asin(std::clamp(norm.y, -1.0f, 1.0f)) / kPi;

        vertices.push_back(Vertex{
            .position = pos,
            .color = glm::vec3(1.0f),
            .uv = glm::vec2(u, v),
            .normal = norm
        });
    }

    return std::make_unique<Mesh>(device, transferCommandPool, vertices, indices, name);
}

// ============================================================================
// B. High-Polygon Torus
// ============================================================================
std::unique_ptr<Mesh> ProceduralMeshes::createTorus(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    float majorRadius,
    float minorRadius,
    uint32_t radialSegments,
    uint32_t tubularSegments,
    std::string_view name
) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve((radialSegments + 1) * (tubularSegments + 1));
    indices.reserve(radialSegments * tubularSegments * 6);

    for (uint32_t i = 0; i <= radialSegments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(radialSegments);
        const float phi = u * kTwoPi;
        const float cosPhi = std::cos(phi);
        const float sinPhi = std::sin(phi);

        for (uint32_t j = 0; j <= tubularSegments; ++j) {
            const float v = static_cast<float>(j) / static_cast<float>(tubularSegments);
            const float theta = v * kTwoPi;
            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);

            const float r = majorRadius + minorRadius * cosTheta;
            const glm::vec3 pos(r * cosPhi, minorRadius * sinTheta, r * sinPhi);
            const glm::vec3 norm = glm::normalize(glm::vec3(cosTheta * cosPhi, sinTheta, cosTheta * sinPhi));

            vertices.push_back(Vertex{
                .position = pos,
                .color = glm::vec3(1.0f),
                .uv = glm::vec2(u, v),
                .normal = norm
            });
        }
    }

    const uint32_t stride = tubularSegments + 1;
    for (uint32_t i = 0; i < radialSegments; ++i) {
        for (uint32_t j = 0; j < tubularSegments; ++j) {
            const uint32_t p0 = i * stride + j;
            const uint32_t p1 = (i + 1) * stride + j;
            const uint32_t p2 = (i + 1) * stride + (j + 1);
            const uint32_t p3 = i * stride + (j + 1);

            indices.push_back(p0);
            indices.push_back(p1);
            indices.push_back(p2);

            indices.push_back(p0);
            indices.push_back(p2);
            indices.push_back(p3);
        }
    }

    return std::make_unique<Mesh>(device, transferCommandPool, vertices, indices, name);
}

// ============================================================================
// C. Procedural Torus Knot (Parallel Transport Frame)
// ============================================================================
std::unique_ptr<Mesh> ProceduralMeshes::createTorusKnot(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    uint32_t p,
    uint32_t q,
    float radius,
    float tubeRadius,
    uint32_t curveSegments,
    uint32_t tubeSegments,
    std::string_view name
) {
    auto sampleCurve = [&](float phi) -> glm::vec3 {
        const float r = (std::cos(static_cast<float>(q) * phi) + 2.0f) * radius;
        return glm::vec3(
            r * std::cos(static_cast<float>(p) * phi),
            -std::sin(static_cast<float>(q) * phi) * radius * 1.2f,
            r * std::sin(static_cast<float>(p) * phi)
        );
    };

    // Precompute curve points and numerical tangents
    std::vector<glm::vec3> curvePoints(curveSegments);
    std::vector<glm::vec3> tangents(curveSegments);
    constexpr float kEps = 0.001f;

    for (uint32_t i = 0; i < curveSegments; ++i) {
        const float phi = static_cast<float>(i) / static_cast<float>(curveSegments) * kTwoPi;
        curvePoints[i] = sampleCurve(phi);
        const glm::vec3 forward = sampleCurve(phi + kEps);
        const glm::vec3 backward = sampleCurve(phi - kEps);
        tangents[i] = glm::normalize(forward - backward);
    }

    // Parallel transport frame propagation along the curve
    std::vector<glm::vec3> normals(curveSegments);
    std::vector<glm::vec3> binormals(curveSegments);

    // Initial reference vector
    glm::vec3 initUp = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(tangents[0], initUp)) > 0.9f) {
        initUp = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    normals[0] = glm::normalize(glm::cross(tangents[0], initUp));
    binormals[0] = glm::normalize(glm::cross(tangents[0], normals[0]));

    for (uint32_t i = 1; i < curveSegments; ++i) {
        const glm::vec3& tPrev = tangents[i - 1];
        const glm::vec3& tCurr = tangents[i];
        const glm::vec3 a = glm::cross(tPrev, tCurr);
        const float dot = std::clamp(glm::dot(tPrev, tCurr), -1.0f, 1.0f);

        if (glm::length(a) > 1e-5f) {
            const glm::vec3 axis = glm::normalize(a);
            const float angle = std::acos(dot);
            // Rodrigues rotation of previous normal around axis
            const glm::vec3 n = normals[i - 1];
            normals[i] = glm::normalize(n * std::cos(angle) + glm::cross(axis, n) * std::sin(angle) + axis * glm::dot(axis, n) * (1.0f - std::cos(angle)));
        } else {
            normals[i] = normals[i - 1];
        }
        binormals[i] = glm::normalize(glm::cross(tCurr, normals[i]));
    }

    // Distribute end twist error evenly to ensure seamless closure
    const float endDot = std::clamp(glm::dot(normals[0], normals.back()), -1.0f, 1.0f);
    const float crossVal = glm::dot(tangents[0], glm::cross(normals.back(), normals[0]));
    const float totalTwist = std::atan2(crossVal, endDot);

    for (uint32_t i = 0; i < curveSegments; ++i) {
        const float twistCorrection = totalTwist * (static_cast<float>(i) / static_cast<float>(curveSegments));
        const float c = std::cos(twistCorrection);
        const float s = std::sin(twistCorrection);
        const glm::vec3 correctedN = glm::normalize(normals[i] * c + binormals[i] * s);
        normals[i] = correctedN;
        binormals[i] = glm::normalize(glm::cross(tangents[i], correctedN));
    }

    // Generate tube mesh vertices
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve((curveSegments + 1) * (tubeSegments + 1));
    indices.reserve(curveSegments * tubeSegments * 6);

    for (uint32_t i = 0; i <= curveSegments; ++i) {
        const uint32_t curveIdx = i % curveSegments;
        const glm::vec3& center = curvePoints[curveIdx];
        const glm::vec3& N = normals[curveIdx];
        const glm::vec3& B = binormals[curveIdx];
        const float u = static_cast<float>(i) / static_cast<float>(curveSegments);

        for (uint32_t j = 0; j <= tubeSegments; ++j) {
            const float v = static_cast<float>(j) / static_cast<float>(tubeSegments);
            const float theta = v * kTwoPi;
            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);

            const glm::vec3 norm = glm::normalize(N * cosTheta + B * sinTheta);
            const glm::vec3 pos = center + norm * tubeRadius;

            vertices.push_back(Vertex{
                .position = pos,
                .color = glm::vec3(1.0f),
                .uv = glm::vec2(u * static_cast<float>(p + q), v),
                .normal = norm
            });
        }
    }

    const uint32_t stride = tubeSegments + 1;
    for (uint32_t i = 0; i < curveSegments; ++i) {
        for (uint32_t j = 0; j < tubeSegments; ++j) {
            const uint32_t p0 = i * stride + j;
            const uint32_t p1 = (i + 1) * stride + j;
            const uint32_t p2 = (i + 1) * stride + (j + 1);
            const uint32_t p3 = i * stride + (j + 1);

            indices.push_back(p0);
            indices.push_back(p1);
            indices.push_back(p2);

            indices.push_back(p0);
            indices.push_back(p2);
            indices.push_back(p3);
        }
    }

    return std::make_unique<Mesh>(device, transferCommandPool, vertices, indices, name);
}

// ============================================================================
// D. Detailed Mechanical Gear
// ============================================================================
std::unique_ptr<Mesh> ProceduralMeshes::createGear(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    uint32_t numTeeth,
    float outerRadius,
    float rootRadius,
    float boreRadius,
    float thickness,
    uint32_t radialRings,
    std::string_view name
) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const float halfThick = thickness * 0.5f;
    const float webHalfThick = halfThick * 0.65f;
    const float webInnerRadius = boreRadius * 1.55f;
    const float webOuterRadius = rootRadius * 0.88f;

    // 8 perimeter steps per tooth for a smooth involute-like profile
    const uint32_t stepsPerTooth = 8;
    const uint32_t totalPerimeterSteps = numTeeth * stepsPerTooth;
    const float angleStep = kTwoPi / static_cast<float>(totalPerimeterSteps);

    // Compute 2D profile radius for each perimeter step (smooth flank transition)
    std::vector<float> perimRadii(totalPerimeterSteps);
    for (uint32_t t = 0; t < numTeeth; ++t) {
        const uint32_t base = t * stepsPerTooth;
        for (uint32_t s = 0; s < stepsPerTooth; ++s) {
            // Root (0..1), Rising flank (2..3), Tip (4..5), Falling flank (6..7)
            if (s < 2) {
                perimRadii[base + s] = rootRadius;
            } else if (s < 4) {
                float flankT = static_cast<float>(s - 2) / 2.0f;
                perimRadii[base + s] = rootRadius + (outerRadius - rootRadius) * (flankT * 0.5f + 0.5f);
            } else if (s < 6) {
                perimRadii[base + s] = outerRadius;
            } else {
                float flankT = static_cast<float>(s - 6) / 2.0f;
                perimRadii[base + s] = outerRadius - (outerRadius - rootRadius) * (flankT * 0.5f + 0.5f);
            }
        }
    }

    // Helper to add a quad
    auto addQuad = [&](uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3) {
        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);

        indices.push_back(i0);
        indices.push_back(i2);
        indices.push_back(i3);
    };

    // Subdivided radial rings for top and bottom faces
    const uint32_t numRings = std::max(4u, radialRings);
    std::vector<float> ringRadii(numRings);
    std::vector<float> ringYTop(numRings);
    std::vector<float> ringYBot(numRings);

    for (uint32_t r = 0; r < numRings; ++r) {
        const float t = static_cast<float>(r) / static_cast<float>(numRings - 1);
        if (t < 0.25f) {
            // Bore to web inner
            const float subT = t / 0.25f;
            ringRadii[r] = boreRadius + (webInnerRadius - boreRadius) * subT;
            ringYTop[r] = halfThick + (webHalfThick - halfThick) * subT;
            ringYBot[r] = -ringYTop[r];
        } else if (t < 0.75f) {
            // Recessed web
            const float subT = (t - 0.25f) / 0.50f;
            ringRadii[r] = webInnerRadius + (webOuterRadius - webInnerRadius) * subT;
            ringYTop[r] = webHalfThick;
            ringYBot[r] = -webHalfThick;
        } else {
            // Web outer to root
            const float subT = (t - 0.75f) / 0.25f;
            ringRadii[r] = webOuterRadius + (rootRadius - webOuterRadius) * subT;
            ringYTop[r] = webHalfThick + (halfThick - webHalfThick) * subT;
            ringYBot[r] = -ringYTop[r];
        }
    }

    // Top face (+Y)
    const uint32_t topStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t r = 0; r < numRings; ++r) {
        for (uint32_t step = 0; step < totalPerimeterSteps; ++step) {
            const float phi = static_cast<float>(step) * angleStep;
            float rad = ringRadii[r];
            if (r == numRings - 1) {
                rad = perimRadii[step];
            }
            const glm::vec3 pos(rad * std::cos(phi), ringYTop[r], rad * std::sin(phi));
            vertices.push_back(Vertex{
                .position = pos,
                .color = glm::vec3(1.0f),
                .uv = glm::vec2(pos.x, pos.z) * 0.5f + 0.5f,
                .normal = glm::vec3(0.0f, 1.0f, 0.0f)
            });
        }
    }

    for (uint32_t r = 0; r < numRings - 1; ++r) {
        const uint32_t r0 = topStart + r * totalPerimeterSteps;
        const uint32_t r1 = topStart + (r + 1) * totalPerimeterSteps;
        for (uint32_t step = 0; step < totalPerimeterSteps; ++step) {
            const uint32_t next = (step + 1) % totalPerimeterSteps;
            addQuad(r0 + step, r1 + step, r1 + next, r0 + next);
        }
    }

    // Bottom face (-Y)
    const uint32_t botStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t r = 0; r < numRings; ++r) {
        for (uint32_t step = 0; step < totalPerimeterSteps; ++step) {
            const float phi = static_cast<float>(step) * angleStep;
            float rad = ringRadii[r];
            if (r == numRings - 1) {
                rad = perimRadii[step];
            }
            const glm::vec3 pos(rad * std::cos(phi), ringYBot[r], rad * std::sin(phi));
            vertices.push_back(Vertex{
                .position = pos,
                .color = glm::vec3(1.0f),
                .uv = glm::vec2(pos.x, pos.z) * 0.5f + 0.5f,
                .normal = glm::vec3(0.0f, -1.0f, 0.0f)
            });
        }
    }

    for (uint32_t r = 0; r < numRings - 1; ++r) {
        const uint32_t r0 = botStart + r * totalPerimeterSteps;
        const uint32_t r1 = botStart + (r + 1) * totalPerimeterSteps;
        for (uint32_t step = 0; step < totalPerimeterSteps; ++step) {
            const uint32_t next = (step + 1) % totalPerimeterSteps;
            addQuad(r0 + step, r0 + next, r1 + next, r1 + step);
        }
    }

    // Outer gear teeth perimeter wall (subdivided vertically into 4 segments)
    constexpr uint32_t kWallYSubdivs = 4;
    for (uint32_t step = 0; step < totalPerimeterSteps; ++step) {
        const float phi0 = static_cast<float>(step) * angleStep;
        const float phi1 = static_cast<float>((step + 1) % totalPerimeterSteps) * angleStep;
        const float r0 = perimRadii[step];
        const float r1 = perimRadii[(step + 1) % totalPerimeterSteps];

        const glm::vec3 pTop0(r0 * std::cos(phi0), halfThick, r0 * std::sin(phi0));
        const glm::vec3 pBot0(r0 * std::cos(phi0), -halfThick, r0 * std::sin(phi0));
        const glm::vec3 pBot1(r1 * std::cos(phi1), -halfThick, r1 * std::sin(phi1));

        const glm::vec3 faceNorm = computeFaceNormal(pTop0, pBot0, pBot1);
        const uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

        for (uint32_t y = 0; y <= kWallYSubdivs; ++y) {
            const float yT = static_cast<float>(y) / static_cast<float>(kWallYSubdivs);
            const float yPos = halfThick - yT * thickness;
            vertices.push_back(Vertex{.position = glm::vec3(r0 * std::cos(phi0), yPos, r0 * std::sin(phi0)), .color = glm::vec3(1.0f), .uv = glm::vec2(0.0f, yT), .normal = faceNorm});
            vertices.push_back(Vertex{.position = glm::vec3(r1 * std::cos(phi1), yPos, r1 * std::sin(phi1)), .color = glm::vec3(1.0f), .uv = glm::vec2(1.0f, yT), .normal = faceNorm});
        }

        for (uint32_t y = 0; y < kWallYSubdivs; ++y) {
            const uint32_t v0 = baseIdx + y * 2;
            const uint32_t v1 = baseIdx + (y + 1) * 2;
            const uint32_t v2 = baseIdx + (y + 1) * 2 + 1;
            const uint32_t v3 = baseIdx + y * 2 + 1;
            addQuad(v0, v1, v2, v3);
        }
    }

    // Inner bore cylinder (subdivided vertically into 4 segments)
    for (uint32_t step = 0; step < totalPerimeterSteps; ++step) {
        const float phi0 = static_cast<float>(step) * angleStep;
        const float phi1 = static_cast<float>((step + 1) % totalPerimeterSteps) * angleStep;

        const glm::vec3 n0 = glm::normalize(glm::vec3(-std::cos(phi0), 0.0f, -std::sin(phi0)));
        const glm::vec3 n1 = glm::normalize(glm::vec3(-std::cos(phi1), 0.0f, -std::sin(phi1)));
        const uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

        for (uint32_t y = 0; y <= kWallYSubdivs; ++y) {
            const float yT = static_cast<float>(y) / static_cast<float>(kWallYSubdivs);
            const float yPos = halfThick - yT * thickness;
            vertices.push_back(Vertex{.position = glm::vec3(boreRadius * std::cos(phi0), yPos, boreRadius * std::sin(phi0)), .color = glm::vec3(1.0f), .uv = glm::vec2(0.0f, yT), .normal = n0});
            vertices.push_back(Vertex{.position = glm::vec3(boreRadius * std::cos(phi1), yPos, boreRadius * std::sin(phi1)), .color = glm::vec3(1.0f), .uv = glm::vec2(1.0f, yT), .normal = n1});
        }

        for (uint32_t y = 0; y < kWallYSubdivs; ++y) {
            const uint32_t v0 = baseIdx + y * 2;
            const uint32_t v1 = baseIdx + y * 2 + 1;
            const uint32_t v2 = baseIdx + (y + 1) * 2 + 1;
            const uint32_t v3 = baseIdx + (y + 1) * 2;
            addQuad(v0, v1, v2, v3);
        }
    }

    return std::make_unique<Mesh>(device, transferCommandPool, vertices, indices, name);
}

// ============================================================================
// E. Detailed Multi-Part Mechanical Turbine Rotor
// ============================================================================
std::unique_ptr<Mesh> ProceduralMeshes::createTurbine(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    uint32_t numBlades,
    float hubRadius,
    float tipRadius,
    float hubLength,
    float bladeTwistDeg,
    uint32_t bladeSpanSegments,
    uint32_t bladeChordSegments,
    std::string_view name
) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    auto addQuad = [&](uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3) {
        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);

        indices.push_back(i0);
        indices.push_back(i2);
        indices.push_back(i3);
    };

    // 1. Central Aerodynamic Nose Cone (parabolic nose + cylinder)
    constexpr uint32_t kHubRadialSegs = 64;
    constexpr uint32_t kConeLayers = 16;
    const float halfLen = hubLength * 0.5f;

    // Nose cone tip vertex
    const uint32_t tipIdx = static_cast<uint32_t>(vertices.size());
    vertices.push_back(Vertex{
        .position = glm::vec3(0.0f, 0.0f, halfLen + hubRadius * 1.2f),
        .color = glm::vec3(1.0f),
        .uv = glm::vec2(0.5f, 0.5f),
        .normal = glm::vec3(0.0f, 0.0f, 1.0f)
    });

    // Parabolic cone rings
    for (uint32_t layer = 1; layer <= kConeLayers; ++layer) {
        const float t = static_cast<float>(layer) / static_cast<float>(kConeLayers);
        const float r = hubRadius * std::sin(t * kPi * 0.5f);
        const float z = halfLen + hubRadius * 1.2f * std::cos(t * kPi * 0.5f);

        for (uint32_t seg = 0; seg < kHubRadialSegs; ++seg) {
            const float phi = static_cast<float>(seg) / static_cast<float>(kHubRadialSegs) * kTwoPi;
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);

            const glm::vec3 pos(r * cosPhi, r * sinPhi, z);
            const glm::vec3 norm = glm::normalize(glm::vec3(cosPhi, sinPhi, 0.45f * (1.0f - t)));

            vertices.push_back(Vertex{
                .position = pos,
                .color = glm::vec3(1.0f),
                .uv = glm::vec2(cosPhi, sinPhi) * 0.5f + 0.5f,
                .normal = norm
            });
        }
    }

    // Connect tip to first layer
    for (uint32_t seg = 0; seg < kHubRadialSegs; ++seg) {
        const uint32_t next = (seg + 1) % kHubRadialSegs;
        indices.push_back(tipIdx);
        indices.push_back(1 + seg);
        indices.push_back(1 + next);
    }

    // Connect cone layers
    for (uint32_t layer = 0; layer < kConeLayers - 1; ++layer) {
        const uint32_t r0 = 1 + layer * kHubRadialSegs;
        const uint32_t r1 = 1 + (layer + 1) * kHubRadialSegs;
        for (uint32_t seg = 0; seg < kHubRadialSegs; ++seg) {
            const uint32_t next = (seg + 1) % kHubRadialSegs;
            addQuad(r0 + seg, r1 + seg, r1 + next, r0 + next);
        }
    }

    // 2. Cylindrical Hub Body
    const uint32_t hubBaseStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t seg = 0; seg < kHubRadialSegs; ++seg) {
        const float phi = static_cast<float>(seg) / static_cast<float>(kHubRadialSegs) * kTwoPi;
        const glm::vec3 n(std::cos(phi), std::sin(phi), 0.0f);

        vertices.push_back(Vertex{.position = glm::vec3(hubRadius * n.x, hubRadius * n.y, halfLen), .color = glm::vec3(1.0f), .uv = glm::vec2(0.0f, 1.0f), .normal = n});
        vertices.push_back(Vertex{.position = glm::vec3(hubRadius * n.x, hubRadius * n.y, -halfLen), .color = glm::vec3(1.0f), .uv = glm::vec2(1.0f, 0.0f), .normal = n});
    }

    for (uint32_t seg = 0; seg < kHubRadialSegs; ++seg) {
        const uint32_t next = (seg + 1) % kHubRadialSegs;
        const uint32_t p0 = hubBaseStart + seg * 2;
        const uint32_t p1 = hubBaseStart + seg * 2 + 1;
        const uint32_t p2 = hubBaseStart + next * 2 + 1;
        const uint32_t p3 = hubBaseStart + next * 2;
        addQuad(p0, p1, p2, p3);
    }

    // 3. Twisted Aerodynamic Turbine Blades
    const uint32_t kSpanStations = std::max(6u, bladeSpanSegments);
    const uint32_t kChordPoints = std::max(8u, bladeChordSegments);

    for (uint32_t b = 0; b < numBlades; ++b) {
        const float bladeAngle = static_cast<float>(b) / static_cast<float>(numBlades) * kTwoPi;
        const uint32_t bladeVertexStart = static_cast<uint32_t>(vertices.size());

        for (uint32_t s = 0; s <= kSpanStations; ++s) {
            const float spanT = static_cast<float>(s) / static_cast<float>(kSpanStations);
            const float r = hubRadius + spanT * (tipRadius - hubRadius);
            const float twistAngle = glm::radians(bladeTwistDeg * (1.0f - spanT * 0.70f));
            const float chordLen = 0.14f * (1.0f - spanT * 0.35f);
            const float thicknessVal = 0.025f * (1.0f - spanT * 0.45f);

            for (uint32_t c = 0; c <= kChordPoints; ++c) {
                const float chordT = static_cast<float>(c) / static_cast<float>(kChordPoints);
                const float xChord = (chordT - 0.5f) * chordLen;
                const float camber = 0.04f * std::sin(chordT * kPi) * chordLen;
                const float halfThick = thicknessVal * std::sin(chordT * kPi);
                const float yThick = (c <= kChordPoints / 2) ? (camber + halfThick) : (camber - halfThick);

                const float rx = xChord * std::cos(twistAngle) - yThick * std::sin(twistAngle);
                const float rz = xChord * std::sin(twistAngle) + yThick * std::cos(twistAngle);

                const glm::vec3 bladePos(
                    (r + yThick * 0.2f) * std::cos(bladeAngle) - rx * std::sin(bladeAngle),
                    (r + yThick * 0.2f) * std::sin(bladeAngle) + rx * std::cos(bladeAngle),
                    rz
                );

                const glm::vec3 bladeNorm = glm::normalize(glm::vec3(
                    -std::sin(bladeAngle) * std::cos(twistAngle),
                     std::cos(bladeAngle) * std::cos(twistAngle),
                     std::sin(twistAngle)
                ));

                vertices.push_back(Vertex{
                    .position = bladePos,
                    .color = glm::vec3(1.0f),
                    .uv = glm::vec2(spanT, chordT),
                    .normal = bladeNorm
                });
            }
        }

        const uint32_t bladeStride = kChordPoints + 1;
        for (uint32_t s = 0; s < kSpanStations; ++s) {
            for (uint32_t c = 0; c < kChordPoints; ++c) {
                const uint32_t p0 = bladeVertexStart + s * bladeStride + c;
                const uint32_t p1 = bladeVertexStart + (s + 1) * bladeStride + c;
                const uint32_t p2 = bladeVertexStart + (s + 1) * bladeStride + (c + 1);
                const uint32_t p3 = bladeVertexStart + s * bladeStride + (c + 1);
                addQuad(p0, p1, p2, p3);
            }
        }
    }

    // 4. Outer Retaining Shroud Ring
    const float ringInner = tipRadius * 0.99f;
    const float ringOuter = tipRadius * 1.05f;
    const float ringHalfWidth = hubLength * 0.35f;
    const uint32_t ringStart = static_cast<uint32_t>(vertices.size());

    for (uint32_t seg = 0; seg <= kHubRadialSegs; ++seg) {
        const float phi = static_cast<float>(seg) / static_cast<float>(kHubRadialSegs) * kTwoPi;
        const float cosPhi = std::cos(phi);
        const float sinPhi = std::sin(phi);

        vertices.push_back(Vertex{.position = glm::vec3(ringOuter * cosPhi, ringOuter * sinPhi, ringHalfWidth), .color = glm::vec3(1.0f), .uv = glm::vec2(0.0f, 1.0f), .normal = glm::vec3(cosPhi, sinPhi, 0.0f)});
        vertices.push_back(Vertex{.position = glm::vec3(ringOuter * cosPhi, ringOuter * sinPhi, -ringHalfWidth), .color = glm::vec3(1.0f), .uv = glm::vec2(1.0f, 1.0f), .normal = glm::vec3(cosPhi, sinPhi, 0.0f)});
        vertices.push_back(Vertex{.position = glm::vec3(ringInner * cosPhi, ringInner * sinPhi, ringHalfWidth), .color = glm::vec3(1.0f), .uv = glm::vec2(0.0f, 0.0f), .normal = glm::vec3(-cosPhi, -sinPhi, 0.0f)});
        vertices.push_back(Vertex{.position = glm::vec3(ringInner * cosPhi, ringInner * sinPhi, -ringHalfWidth), .color = glm::vec3(1.0f), .uv = glm::vec2(1.0f, 0.0f), .normal = glm::vec3(-cosPhi, -sinPhi, 0.0f)});
    }

    for (uint32_t seg = 0; seg < kHubRadialSegs; ++seg) {
        const uint32_t b0 = ringStart + seg * 4;
        const uint32_t b1 = ringStart + (seg + 1) * 4;

        addQuad(b0 + 0, b0 + 1, b1 + 1, b1 + 0);
        addQuad(b0 + 2, b1 + 2, b1 + 3, b0 + 3);
        addQuad(b0 + 0, b1 + 0, b1 + 2, b0 + 2);
        addQuad(b0 + 1, b0 + 3, b1 + 3, b1 + 1);
    }

    return std::make_unique<Mesh>(device, transferCommandPool, vertices, indices, name);
}

} // namespace sidera
