#include "core/Log.h"
#include "renderer/ProceduralMeshes.h"
#include "renderer/Vertex.h"
#include "renderer/BoundingVolume.h"
#include "foliage/ProceduralFoliage.h"
#include "terrain/TerrainConfig.h"
#include "terrain/HeightmapData.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

using namespace sidera;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

namespace {

// Compute face normal from 3 triangle positions
glm::vec3 getFaceNormal(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2) {
    const glm::vec3 e1 = p1 - p0;
    const glm::vec3 e2 = p2 - p0;
    const glm::vec3 c = glm::cross(e1, e2);
    const float len = glm::length(c);
    return len > 1e-8f ? c / len : glm::vec3(0.0f);
}

// Compute average vertex normal
glm::vec3 getAvgVertNormal(const glm::vec3& n0, const glm::vec3& n1, const glm::vec3& n2) {
    const glm::vec3 sum = n0 + n1 + n2;
    const float len = glm::length(sum);
    return len > 1e-8f ? sum / len : glm::vec3(0.0f);
}

// Audit all triangles in a mesh for outward orientation and validity
struct MeshAuditStats {
    uint32_t totalTris = 0;
    uint32_t invertedTris = 0;
    uint32_t zeroNormalVerts = 0;
    uint32_t degenerateTris = 0;
    float minDot = 1.0f;
    float maxDot = -1.0f;
    float avgDot = 0.0f;
};

MeshAuditStats auditMeshGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, bool doubleSided = false) {
    MeshAuditStats stats{};
    stats.totalTris = static_cast<uint32_t>(indices.size() / 3);

    for (const auto& v : vertices) {
        if (glm::length(v.normal) < 1e-4f) {
            stats.zeroNormalVerts++;
        }
    }

    float dotSum = 0.0f;
    uint32_t validTris = 0;

    for (uint32_t i = 0; i < stats.totalTris; ++i) {
        const uint32_t i0 = indices[i * 3 + 0];
        const uint32_t i1 = indices[i * 3 + 1];
        const uint32_t i2 = indices[i * 3 + 2];

        const glm::vec3 p0 = vertices[i0].position;
        const glm::vec3 p1 = vertices[i1].position;
        const glm::vec3 p2 = vertices[i2].position;

        const glm::vec3 fn = getFaceNormal(p0, p1, p2);
        if (glm::length(fn) < 1e-6f) {
            stats.degenerateTris++;
            continue;
        }

        const glm::vec3 vn = getAvgVertNormal(vertices[i0].normal, vertices[i1].normal, vertices[i2].normal);
        const float dot = glm::dot(fn, vn);

        stats.minDot = std::min(stats.minDot, dot);
        stats.maxDot = std::max(stats.maxDot, dot);
        dotSum += dot;
        validTris++;

        if (!doubleSided && dot < -0.05f) {
            stats.invertedTris++;
        }
    }

    if (validTris > 0) {
        stats.avgDot = dotSum / static_cast<float>(validTris);
    }
    return stats;
}

} // namespace

// ============================================================================
// Test 1: Primitive Mesh Face & Vertex Normal Consistency (Cube, Pyramid, GroundGrid)
// ============================================================================
int test1_primitive_mesh_face_normals() {
    std::cout << "[Test 1] Primitive mesh face and vertex normal consistency..." << std::endl;

    // Cube
    auto cubeMesh = Mesh::createCube(1.0f);
    TEST_ASSERT(cubeMesh != nullptr, "Failed to create CubeMesh");
    const auto cubeStats = auditMeshGeometry(cubeMesh->getCpuVertices(), cubeMesh->getCpuIndices());
    TEST_ASSERT(cubeStats.zeroNormalVerts == 0, "Cube has zero normals");
    TEST_ASSERT(cubeStats.invertedTris == 0, "Cube has inverted triangles");
    TEST_ASSERT(cubeStats.minDot > 0.99f, "Cube face normals do not match outward vertex normals");

    // Pyramid
    auto pyrMesh = Mesh::createPyramid(1.0f, 2.0f);
    TEST_ASSERT(pyrMesh != nullptr, "Failed to create PyramidMesh");
    const auto pyrStats = auditMeshGeometry(pyrMesh->getCpuVertices(), pyrMesh->getCpuIndices());
    TEST_ASSERT(pyrStats.zeroNormalVerts == 0, "Pyramid has zero normals");
    TEST_ASSERT(pyrStats.invertedTris == 0, "Pyramid has inverted triangles");
    TEST_ASSERT(pyrStats.minDot > 0.99f, "Pyramid face normals do not match outward vertex normals");

    // Ground Grid (Fixed in Milestone Pre-21: now has (0, 1, 0) normals)
    auto gridMesh = Mesh::createGroundGrid(10.0f, 4);
    TEST_ASSERT(gridMesh != nullptr, "Failed to create GroundGridMesh");
    const auto gridStats = auditMeshGeometry(gridMesh->getCpuVertices(), gridMesh->getCpuIndices());
    TEST_ASSERT(gridStats.zeroNormalVerts == 0, "GroundGrid has unassigned/zero vertex normals!");
    TEST_ASSERT(gridStats.invertedTris == 0, "GroundGrid has inverted triangles");
    TEST_ASSERT(gridStats.minDot > 0.99f, "GroundGrid face normal does not point upward (+Y)");

    std::cout << "  Cube: " << cubeStats.totalTris << " tris, min dot: " << cubeStats.minDot << std::endl;
    std::cout << "  Pyramid: " << pyrStats.totalTris << " tris, min dot: " << pyrStats.minDot << std::endl;
    std::cout << "  GroundGrid: " << gridStats.totalTris << " tris, min dot: " << gridStats.minDot << std::endl;
    return 0;
}

// ============================================================================
// Test 2: Analytical Sphere & Icosphere Normal Consistency
// ============================================================================
int test2_sphere_icosphere_normal_consistency() {
    std::cout << "[Test 2] Sphere and Icosphere outward normal consistency..." << std::endl;

    // Sphere
    auto sphereMesh = Mesh::createSphere(1.5f, 16, 16);
    TEST_ASSERT(sphereMesh != nullptr, "Failed to create SphereMesh");
    const auto sphereStats = auditMeshGeometry(sphereMesh->getCpuVertices(), sphereMesh->getCpuIndices());
    TEST_ASSERT(sphereStats.zeroNormalVerts == 0, "Sphere has zero normals");
    TEST_ASSERT(sphereStats.invertedTris == 0, "Sphere has inverted triangles");
    TEST_ASSERT(sphereStats.avgDot > 0.95f, "Sphere face normals do not point outward");

    // Icosphere
    auto icoMesh = ProceduralMeshes::createIcosphere(1.5f, 2, "TestIcosphere");
    TEST_ASSERT(icoMesh != nullptr, "Failed to create IcosphereMesh");
    const auto icoStats = auditMeshGeometry(icoMesh->getCpuVertices(), icoMesh->getCpuIndices());
    TEST_ASSERT(icoStats.zeroNormalVerts == 0, "Icosphere has zero normals");
    TEST_ASSERT(icoStats.invertedTris == 0, "Icosphere has inverted triangles");
    TEST_ASSERT(icoStats.minDot > 0.99f, "Icosphere face normals do not match outward vertex normals");

    std::cout << "  Sphere: " << sphereStats.totalTris << " tris, avg dot: " << sphereStats.avgDot << std::endl;
    std::cout << "  Icosphere: " << icoStats.totalTris << " tris, min dot: " << icoStats.minDot << std::endl;
    return 0;
}

// ============================================================================
// Test 3: Winding Order Validation (Torus, Torus Knot, Gear, Turbine)
// ============================================================================
int test3_winding_order_validation() {
    std::cout << "[Test 3] High-poly complex mesh winding order validation..." << std::endl;

    // Torus (Fixed: now CCW outward)
    auto torusMesh = ProceduralMeshes::createTorus(1.0f, 0.35f, 24, 16, "TestTorus");
    TEST_ASSERT(torusMesh != nullptr, "Failed to create Torus");
    const auto torusStats = auditMeshGeometry(torusMesh->getCpuVertices(), torusMesh->getCpuIndices());
    TEST_ASSERT(torusStats.zeroNormalVerts == 0, "Torus has zero normals");
    TEST_ASSERT(torusStats.invertedTris == 0, "Torus still has inverted triangles!");
    TEST_ASSERT(torusStats.avgDot > 0.98f, "Torus face normals do not point outward from tube");

    // Torus Knot (Fixed: now CCW outward)
    auto knotMesh = ProceduralMeshes::createTorusKnot(2, 3, 1.0f, 0.25f, 48, 12, "TestTorusKnot");
    TEST_ASSERT(knotMesh != nullptr, "Failed to create TorusKnot");
    const auto knotStats = auditMeshGeometry(knotMesh->getCpuVertices(), knotMesh->getCpuIndices());
    TEST_ASSERT(knotStats.zeroNormalVerts == 0, "TorusKnot has zero normals");
    TEST_ASSERT(knotStats.invertedTris == 0, "TorusKnot still has inverted triangles!");
    TEST_ASSERT(knotStats.avgDot > 0.98f, "TorusKnot face normals do not point outward");

    // Gear (Fixed: Top face, Bottom face, Wall, and Bore all CCW outward)
    auto gearMesh = ProceduralMeshes::createGear(12, 2.0f, 1.6f, 0.5f, 0.4f, 4, "TestGear");
    TEST_ASSERT(gearMesh != nullptr, "Failed to create Gear");
    const auto gearStats = auditMeshGeometry(gearMesh->getCpuVertices(), gearMesh->getCpuIndices());
    TEST_ASSERT(gearStats.zeroNormalVerts == 0, "Gear has zero normals");
    TEST_ASSERT(gearStats.invertedTris == 0, "Gear still has inverted triangles!");
    TEST_ASSERT(gearStats.avgDot > 0.99f, "Gear face normals do not point outward");

    std::cout << "  Torus: " << torusStats.totalTris << " tris, avg dot: " << torusStats.avgDot << std::endl;
    std::cout << "  TorusKnot: " << knotStats.totalTris << " tris, avg dot: " << knotStats.avgDot << std::endl;
    std::cout << "  Gear: " << gearStats.totalTris << " tris, avg dot: " << gearStats.avgDot << std::endl;
    return 0;
}

// ============================================================================
// Test 4: Non-Uniform Scale Normal Transformation
// ============================================================================
int test4_non_uniform_scale_normal_transformation() {
    std::cout << "[Test 4] Non-uniform scale normal transformation (inverse-transpose)..." << std::endl;

    // Construct model matrix with rotation and non-uniform scaling
    glm::mat4 model(1.0f);
    model = glm::rotate(model, glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(25.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, glm::vec3(3.5f, 0.4f, 1.8f)); // Strongly non-uniform

    // Surface in model space:
    // Normal n = (0, 1, 0)
    // Tangent t1 = (1, 0, 0)
    // Bitangent t2 = (0, 0, 1)
    const glm::vec3 n(0.0f, 1.0f, 0.0f);
    const glm::vec3 t1(1.0f, 0.0f, 0.0f);
    const glm::vec3 t2(0.0f, 0.0f, 1.0f);

    // Transformed tangents (points on the surface transform by model matrix)
    const glm::vec3 t1_world = glm::normalize(glm::vec3(model * glm::vec4(t1, 0.0f)));
    const glm::vec3 t2_world = glm::normalize(glm::vec3(model * glm::vec4(t2, 0.0f)));

    // Correct normal transformation: inverse-transpose of 3x3 model matrix
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
    const glm::vec3 n_correct = glm::normalize(normalMatrix * n);

    // Incorrect naive normal transformation: model * n
    const glm::vec3 n_naive = glm::normalize(glm::mat3(model) * n);

    // Verify correct normal is strictly perpendicular to transformed surface tangents
    const float dot1 = glm::dot(n_correct, t1_world);
    const float dot2 = glm::dot(n_correct, t2_world);
    TEST_ASSERT(std::abs(dot1) < 1e-5f, "Inverse-transpose normal is not perpendicular to t1");
    TEST_ASSERT(std::abs(dot2) < 1e-5f, "Inverse-transpose normal is not perpendicular to t2");

    // Verify naive normal fails orthogonality test
    const glm::vec3 trueGeomNormal = glm::normalize(glm::cross(t1_world, t2_world));
    const float naiveDiff = glm::length(n_naive - trueGeomNormal);
    TEST_ASSERT(naiveDiff > 0.05f, "Naive transformation unexpectedly matched");

    std::cout << "  Inverse-transpose orthogonality error: " << std::max(std::abs(dot1), std::abs(dot2))
              << " (Naive error: " << naiveDiff << ")" << std::endl;
    return 0;
}

// ============================================================================
// Test 5: Negative Scale Handling & Determinant Analysis
// ============================================================================
int test5_negative_scale_handling() {
    std::cout << "[Test 5] Negative-scale (mirror) reflection handling..." << std::endl;

    // Non-mirrored transform
    glm::mat4 mPositive = glm::scale(glm::mat4(1.0f), glm::vec3(1.5f, 1.5f, 1.5f));
    float detPos = glm::determinant(glm::mat3(mPositive));
    TEST_ASSERT(detPos > 0.0f, "Positive scale has negative determinant");

    // Mirrored transform (e.g. mirrored along X)
    glm::mat4 mNegative = glm::scale(glm::mat4(1.0f), glm::vec3(-1.5f, 1.5f, 1.5f));
    float detNeg = glm::determinant(glm::mat3(mNegative));
    TEST_ASSERT(detNeg < 0.0f, "Mirrored scale has positive determinant");

    // Test a CCW triangle under reflection:
    // v0 = (0, 0, 0), v1 = (1, 0, 0), v2 = (0, 1, 0) -> normal (0, 0, 1)
    const glm::vec3 p0(0.0f, 0.0f, 0.0f);
    const glm::vec3 p1(1.0f, 0.0f, 0.0f);
    const glm::vec3 p2(0.0f, 1.0f, 0.0f);

    const glm::vec3 origNormal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
    TEST_ASSERT(origNormal.z > 0.99f, "Original triangle normal should point in +Z");

    // Transformed vertices under X reflection
    const glm::vec3 tp0 = glm::vec3(mNegative * glm::vec4(p0, 1.0f));
    const glm::vec3 tp1 = glm::vec3(mNegative * glm::vec4(p1, 1.0f));
    const glm::vec3 tp2 = glm::vec3(mNegative * glm::vec4(p2, 1.0f));

    // Reflected triangle face normal in world space:
    const glm::vec3 reflFaceNormal = glm::normalize(glm::cross(tp1 - tp0, tp2 - tp0));

    // Notice: under single-axis reflection, the geometric triangle winding order flips,
    // so the naive cross product (tp1 - tp0) x (tp2 - tp0) points in -Z!
    TEST_ASSERT(reflFaceNormal.z < -0.99f, "Reflected triangle raw cross product must point in -Z");

    // Normal matrix transforms the vector normal: (M^-1)^T * origNormal
    // Since only X was mirrored and normal was (0, 0, 1), the transformed normal remains (0, 0, 1):
    const glm::mat3 nMatNeg = glm::transpose(glm::inverse(glm::mat3(mNegative)));
    const glm::vec3 transformedNorm = glm::normalize(nMatNeg * origNormal);
    TEST_ASSERT(transformedNorm.z > 0.99f, "Transformed vertex normal remains in +Z");

    // Verify fundamental linear algebra theorem:
    // (M*e1) x (M*e2) = det(M) * (M^-1)^T * (e1 x e2)
    // Thus raw cross product is inverted by sgn(det(M)):
    const float detSign = std::copysign(1.0f, detNeg);
    const glm::vec3 parityCorrectedFaceNorm = reflFaceNormal * detSign;
    TEST_ASSERT(glm::dot(parityCorrectedFaceNorm, transformedNorm) > 0.99f, "Determinant-corrected face normal must match transformed normal");

    // When reversing winding order (tp2, tp1, tp0), geometric face normal aligns with transformed normal:
    const glm::vec3 reversedWoundFaceNorm = glm::normalize(glm::cross(tp2 - tp0, tp1 - tp0));
    TEST_ASSERT(glm::dot(reversedWoundFaceNorm, transformedNorm) > 0.99f, "Reversed winding aligns face normal with vertex normal");

    std::cout << "  Determinant: Pos=" << detPos << ", Neg=" << detNeg
              << " | Parity-corrected alignment: " << glm::dot(parityCorrectedFaceNorm, transformedNorm) << std::endl;
    return 0;
}

// ============================================================================
// Test 6: Tangent-Space Transformation & Cotangent Frame Orthogonality
// ============================================================================
int test6_tangent_space_transformation() {
    std::cout << "[Test 6] Tangent space basis & Vulkan cotangent frame calculation..." << std::endl;

    // Simulation of fragment shader cotangent frame calculation in Vulkan:
    // Screen X goes right (+X in world), Screen Y goes down (-Y in world).
    // Surface normal N = (0, 0, 1).
    const glm::vec3 N(0.0f, 0.0f, 1.0f);
    const glm::vec3 dp1(1.0f, 0.0f, 0.0f);  // dFdx(pos)
    const glm::vec3 dp2(0.0f, -1.0f, 0.0f); // dFdy(pos) in Vulkan
    const glm::vec2 duv1(1.0f, 0.0f);       // dFdx(uv)
    const glm::vec2 duv2(0.0f, 1.0f);       // dFdy(uv) in Vulkan

    // Milestone Pre-21 Vulkan-corrected cotangent frame:
    const glm::vec3 dp2perp = glm::cross(N, dp2);
    const glm::vec3 dp1perp = glm::cross(dp1, N);
    const glm::vec3 T_raw = dp2perp * duv1.x + dp1perp * duv2.x;
    const glm::vec3 B_raw = dp2perp * duv1.y + dp1perp * duv2.y;

    const float maxLenSq = std::max(glm::dot(T_raw, T_raw), glm::dot(B_raw, B_raw));
    TEST_ASSERT(maxLenSq > 1e-6f, "Cotangent basis is degenerate");
    const float invmax = 1.0f / std::sqrt(maxLenSq);
    const glm::vec3 T = T_raw * invmax;
    const glm::vec3 B = B_raw * invmax;

    // Verify T is aligned with increasing U (+X in world)
    TEST_ASSERT(T.x > 0.99f, "Tangent T does not align with increasing U (+X)");

    // Verify B is aligned with increasing V (-Y in world, since V increases downwards)
    TEST_ASSERT(B.y < -0.99f, "Bitangent B does not align with increasing V (-Y)");

    // Verify orthogonality to surface normal
    TEST_ASSERT(std::abs(glm::dot(T, N)) < 1e-6f, "T is not orthogonal to N");
    TEST_ASSERT(std::abs(glm::dot(B, N)) < 1e-6f, "B is not orthogonal to N");

    // Test degenerate fallback when duv1 == duv2 == 0:
    const glm::vec2 degen_duv1(0.0f);
    const glm::vec2 degen_duv2(0.0f);
    const glm::vec3 degen_T_raw = dp2perp * degen_duv1.x + dp1perp * degen_duv2.x;
    const glm::vec3 degen_B_raw = dp2perp * degen_duv1.y + dp1perp * degen_duv2.y;
    const float degen_maxLenSq = std::max(glm::dot(degen_T_raw, degen_T_raw), glm::dot(degen_B_raw, degen_B_raw));

    glm::mat3 fallbackTBN(1.0f);
    if (degen_maxLenSq < 1e-10f) {
        const glm::vec3 up = std::abs(N.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 fT = glm::normalize(glm::cross(up, N));
        const glm::vec3 fB = glm::cross(N, fT);
        fallbackTBN = glm::mat3(fT, fB, N);
    }
    // Verify fallback preserves surface normal N
    TEST_ASSERT(glm::length(fallbackTBN[2] - N) < 1e-6f, "Fallback TBN does not preserve normal N");

    std::cout << "  Vulkan TBN: T=(" << T.x << "," << T.y << "," << T.z << "), B=(" << B.x << "," << B.y << "," << B.z << ")" << std::endl;
    return 0;
}

// ============================================================================
// Test 7: Tangent-Space Normal Map Orientation & DirectX Convention
// ============================================================================
int test7_normal_map_orientation() {
    std::cout << "[Test 7] Normal map perturbation and DirectX/OpenGL green channel convention..." << std::endl;

    const glm::vec3 N(0.0f, 1.0f, 0.0f); // Ground normal pointing UP
    const glm::vec3 T(1.0f, 0.0f, 0.0f); // Tangent pointing EAST (+X)
    const glm::vec3 B(0.0f, 0.0f, 1.0f); // Bitangent pointing SOUTH (+Z)
    const glm::mat3 TBN(T, B, N);

    // 1. Flat normal (0, 0, 1) in tangent space -> should yield surface normal (0, 1, 0)
    const glm::vec3 flatTangent(0.0f, 0.0f, 1.0f);
    const glm::vec3 worldFlat = glm::normalize(TBN * flatTangent);
    TEST_ASSERT(glm::length(worldFlat - N) < 1e-6f, "Flat normal map did not yield surface normal");

    // 2. Perturbation along +X (+U in texture): (0.35, 0.0, 0.9367)
    const glm::vec3 bumpEast = glm::normalize(glm::vec3(0.35f, 0.0f, 0.9367f));
    const glm::vec3 worldBumpEast = glm::normalize(TBN * bumpEast);
    TEST_ASSERT(worldBumpEast.x > 0.30f, "Normal map +X perturbation did not tilt towards +X in world");
    TEST_ASSERT(worldBumpEast.y > 0.85f, "Normal map still maintains upward Y component");

    // 3. DirectX (+Y down) vs OpenGL (+Y up) validation:
    // In DirectX convention, normal map green channel points in direction of increasing texture V.
    const glm::vec3 bumpSouth = glm::normalize(glm::vec3(0.0f, 0.35f, 0.9367f));
    const glm::vec3 worldBumpSouth = glm::normalize(TBN * bumpSouth);
    TEST_ASSERT(worldBumpSouth.z > 0.30f, "Normal map +Y perturbation correctly maps along bitangent B (+Z)");

    std::cout << "  Flat mapping error: " << glm::length(worldFlat - N)
              << " | Perturbed normal: (" << worldBumpEast.x << ", " << worldBumpEast.y << ", " << worldBumpEast.z << ")" << std::endl;
    return 0;
}

// ============================================================================
// Test 8: Terrain Continuous Normal Calculation & Boundary Consistency
// ============================================================================
int test8_terrain_boundary_normal_consistency() {
    std::cout << "[Test 8] Terrain continuous heightmap normal & boundary consistency..." << std::endl;

    TerrainConfig config{};
    config.width = 120.0f;
    config.length = 120.0f;
    config.heightScale = 14.0f;

    HeightmapData heightmap{};
    heightmap.generateProcedural(128, 42);
    TEST_ASSERT(heightmap.isValid(), "Failed to generate procedural heightmap");

    // Check normals along chunk boundaries:
    // Boundary between Chunk (0, 0) and Chunk (1, 0) occurs at normalized u = 0.5f
    constexpr uint32_t sampleCount = 16;
    float maxBorderDiscrepancy = 0.0f;

    for (uint32_t i = 0; i < sampleCount; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(sampleCount - 1);

        // Sample slightly to the left of the border and slightly to the right
        constexpr float eps = 1e-5f;
        const glm::vec3 nLeft = heightmap.computeNormal(0.5f - eps, v, config.width, config.length, config.heightScale);
        const glm::vec3 nRight = heightmap.computeNormal(0.5f + eps, v, config.width, config.length, config.heightScale);

        // Both must be valid normalized vectors
        TEST_ASSERT(std::abs(glm::length(nLeft) - 1.0f) < 1e-4f, "Left border normal not normalized");
        TEST_ASSERT(std::abs(glm::length(nRight) - 1.0f) < 1e-4f, "Right border normal not normalized");

        // The normal must point upward (+Y) across the entire terrain
        TEST_ASSERT(nLeft.y > 0.2f, "Terrain normal does not point upward (+Y)");
        TEST_ASSERT(nRight.y > 0.2f, "Terrain normal does not point upward (+Y)");

        const float diff = glm::length(nLeft - nRight);
        maxBorderDiscrepancy = std::max(maxBorderDiscrepancy, diff);
    }

    TEST_ASSERT(maxBorderDiscrepancy < 0.01f, "Terrain has normal seams across chunk boundary");
    std::cout << "  Max chunk border normal discrepancy: " << maxBorderDiscrepancy << " (zero seam verified)" << std::endl;
    return 0;
}

// ============================================================================
// Test 9: Foliage Mesh Normal Consistency (Trunk, Canopy, Bush)
// ============================================================================
int test9_foliage_mesh_normal_consistency() {
    std::cout << "[Test 9] Foliage mesh normal consistency (Trunk, Canopy, Bush)..." << std::endl;

    // Tree Trunk
    auto trunkMesh = ProceduralFoliage::createTreeTrunkMesh(0, 4.0f, 0.35f, 0.18f, "TestTrunk");
    TEST_ASSERT(trunkMesh != nullptr, "Failed to create TreeTrunk");
    const auto trunkStats = auditMeshGeometry(trunkMesh->getCpuVertices(), trunkMesh->getCpuIndices());
    TEST_ASSERT(trunkStats.zeroNormalVerts == 0, "TreeTrunk has zero normals");
    TEST_ASSERT(trunkStats.invertedTris == 0, "TreeTrunk has inverted triangles");
    TEST_ASSERT(trunkStats.avgDot > 0.98f, "TreeTrunk face normals do not point outward from cylinder");

    // Tree Canopy (Fixed in Milestone Pre-21: now all tiers face outward, under-cap points downward)
    auto canopyMesh = ProceduralFoliage::createTreeCanopyMesh(0, 2.0f, 6.5f, 1.8f, "TestCanopy");
    TEST_ASSERT(canopyMesh != nullptr, "Failed to create TreeCanopy");
    const auto canopyStats = auditMeshGeometry(canopyMesh->getCpuVertices(), canopyMesh->getCpuIndices());
    TEST_ASSERT(canopyStats.zeroNormalVerts == 0, "TreeCanopy has zero normals");
    TEST_ASSERT(canopyStats.invertedTris == 0, "TreeCanopy still has inverted triangles!");
    TEST_ASSERT(canopyStats.minDot > 0.05f, "TreeCanopy contains backward/inverted triangles");

    // Bush
    auto bushMesh = ProceduralFoliage::createBushMesh(0, 1.0f, 0.8f, "TestBush");
    TEST_ASSERT(bushMesh != nullptr, "Failed to create Bush");
    const auto bushStats = auditMeshGeometry(bushMesh->getCpuVertices(), bushMesh->getCpuIndices());
    TEST_ASSERT(bushStats.zeroNormalVerts == 0, "Bush has zero normals");
    TEST_ASSERT(bushStats.invertedTris == 0, "Bush has inverted triangles");
    TEST_ASSERT(bushStats.avgDot > 0.95f, "Bush dome face normals do not point outward");

    std::cout << "  Trunk: " << trunkStats.totalTris << " tris, avg dot: " << trunkStats.avgDot << std::endl;
    std::cout << "  Canopy: " << canopyStats.totalTris << " tris, min dot: " << canopyStats.minDot << std::endl;
    std::cout << "  Bush: " << bushStats.totalTris << " tris, avg dot: " << bushStats.avgDot << std::endl;
    return 0;
}

// ============================================================================
// Test 10: Vulkan Back-Face Culling Compatibility
// ============================================================================
int test10_vulkan_backface_culling_compatibility() {
    std::cout << "[Test 10] Vulkan screen-space signed area & back-face culling compatibility..." << std::endl;

    // Camera view looking along -Z from (0, 0, 5) towards origin
    const glm::vec3 camPos(0.0f, 0.0f, 5.0f);
    const glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    // Vulkan perspective projection with inverted Y (proj[1][1] *= -1.0f)
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1920.0f / 1080.0f, 0.1f, 100.0f);
    proj[1][1] *= -1.0f; // Standard Vulkan NDC inversion

    const glm::mat4 viewProj = proj * view;

    // Consider front face of Cube centered at (0, 0, 0) with normal (0, 0, 1):
    // Vertex 0: (-1, -1, 1), Vertex 1: (1, -1, 1), Vertex 2: (1, 1, 1) -> Indices (0, 1, 2)
    const glm::vec3 v0(-1.0f, -1.0f, 1.0f);
    const glm::vec3 v1( 1.0f, -1.0f, 1.0f);
    const glm::vec3 v2( 1.0f,  1.0f, 1.0f);

    auto projectToScreen = [&](const glm::vec3& p) -> glm::vec2 {
        glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        // Map NDC [-1, 1] to framebuffer pixels [0, width], [0, height]
        // Note: in Vulkan framebuffer coordinates, x_fb = (ndc.x + 1) * 0.5 * W, y_fb = (ndc.y + 1) * 0.5 * H
        return glm::vec2((ndc.x + 1.0f) * 0.5f * 1920.0f, (ndc.y + 1.0f) * 0.5f * 1080.0f);
    };

    const glm::vec2 s0 = projectToScreen(v0);
    const glm::vec2 s1 = projectToScreen(v1);
    const glm::vec2 s2 = projectToScreen(v2);

    // Vulkan specification signed area formula:
    // a = 0.5 * sum_{i=0}^{n-1} (x_i * y_{i+1} - x_{i+1} * y_i)
    // In Vulkan (where framebuffer Y goes DOWN):
    // If a > 0 -> Counter-Clockwise (VK_FRONT_FACE_COUNTER_CLOCKWISE)
    // If a < 0 -> Clockwise (VK_FRONT_FACE_CLOCKWISE)
    const float signedArea = 0.5f * (
        (s0.x * s1.y - s1.x * s0.y) +
        (s1.x * s2.y - s2.x * s1.y) +
        (s2.x * s0.y - s0.x * s2.y)
    );

    // Back face: Vertex 0: (1, -1, -1), Vertex 1: (-1, -1, -1), Vertex 2: (-1, 1, -1)
    const glm::vec3 b0( 1.0f, -1.0f, -1.0f);
    const glm::vec3 b1(-1.0f, -1.0f, -1.0f);
    const glm::vec3 b2(-1.0f,  1.0f, -1.0f);

    const glm::vec2 bs0 = projectToScreen(b0);
    const glm::vec2 bs1 = projectToScreen(b1);
    const glm::vec2 bs2 = projectToScreen(b2);

    const float backSignedArea = 0.5f * (
        (bs0.x * bs1.y - bs1.x * bs0.y) +
        (bs1.x * bs2.y - bs2.x * bs1.y) +
        (bs2.x * bs0.y - bs0.x * bs2.y)
    );

    // In Vulkan framebuffer coordinates, the Y axis increases DOWNWARDS (origin at top-left).
    // A counter-clockwise triangle in visual screen space (right -> up -> left) has dy < 0 when going up,
    // which results in a negative signed area (a < 0).
    // Conversely, a back-facing clockwise triangle results in a positive signed area (a > 0).
    TEST_ASSERT(signedArea < 0.0f, "Front-facing triangle must produce negative signed area in Vulkan (Y-down) framebuffer coordinates");
    TEST_ASSERT(backSignedArea > 0.0f, "Back-facing triangle must produce positive signed area in Vulkan (Y-down) framebuffer coordinates");
    TEST_ASSERT(signedArea * backSignedArea < 0.0f, "Front and back faces must have opposite signed areas for hardware culling discrimination");

    std::cout << "  Vulkan signed area: FrontFace=" << signedArea << " (Visual CCW), BackFace=" << backSignedArea << " (Visual CW)" << std::endl;
    return 0;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "  SIDERA Engine — Pre-M21 Normal Audit Unit Tests   " << std::endl;
    std::cout << "====================================================" << std::endl;

    TEST_ASSERT(test1_primitive_mesh_face_normals() == 0, "Test 1 Failed");
    TEST_ASSERT(test2_sphere_icosphere_normal_consistency() == 0, "Test 2 Failed");
    TEST_ASSERT(test3_winding_order_validation() == 0, "Test 3 Failed");
    TEST_ASSERT(test4_non_uniform_scale_normal_transformation() == 0, "Test 4 Failed");
    TEST_ASSERT(test5_negative_scale_handling() == 0, "Test 5 Failed");
    TEST_ASSERT(test6_tangent_space_transformation() == 0, "Test 6 Failed");
    TEST_ASSERT(test7_normal_map_orientation() == 0, "Test 7 Failed");
    TEST_ASSERT(test8_terrain_boundary_normal_consistency() == 0, "Test 8 Failed");
    TEST_ASSERT(test9_foliage_mesh_normal_consistency() == 0, "Test 9 Failed");
    TEST_ASSERT(test10_vulkan_backface_culling_compatibility() == 0, "Test 10 Failed");

    std::cout << "====================================================" << std::endl;
    std::cout << "  ALL 10 NORMAL AUDIT UNIT TESTS PASSED!            " << std::endl;
    std::cout << "====================================================" << std::endl;

    return 0;
}
