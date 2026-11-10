#include "lod/LODConfig.h"
#include "lod/LODGroup.h"
#include "lod/LODLevel.h"
#include "lod/LODManager.h"
#include "lod/LODStats.h"
#include "lod/MeshSimplifier.h"
#include "renderer/Vertex.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <iostream>
#include <vector>

using namespace sidera;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

// Helper: Generates a test sphere mesh CPU vertices and indices
void generateTestSphere(uint32_t stacks, uint32_t slices, float radius,
                        std::vector<Vertex>& outVertices,
                        std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    for (uint32_t i = 0; i <= stacks; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(stacks);
        const float phi = v * 3.14159265359f;

        for (uint32_t j = 0; j <= slices; ++j) {
            const float u = static_cast<float>(j) / static_cast<float>(slices);
            const float theta = u * 2.0f * 3.14159265359f;

            const float x = radius * std::sin(phi) * std::cos(theta);
            const float y = radius * std::cos(phi);
            const float z = radius * std::sin(phi) * std::sin(theta);

            Vertex vert{};
            vert.position = glm::vec3(x, y, z);
            vert.normal = glm::normalize(vert.position);
            vert.uv = glm::vec2(u, v);
            vert.color = glm::vec3(1.0f);
            outVertices.push_back(vert);
        }
    }

    const uint32_t stride = slices + 1;
    for (uint32_t i = 0; i < stacks; ++i) {
        for (uint32_t j = 0; j < slices; ++j) {
            const uint32_t i0 = i * stride + j;
            const uint32_t i1 = (i + 1) * stride + j;
            const uint32_t i2 = (i + 1) * stride + (j + 1);
            const uint32_t i3 = i * stride + (j + 1);

            outIndices.push_back(i0);
            outIndices.push_back(i1);
            outIndices.push_back(i2);

            outIndices.push_back(i0);
            outIndices.push_back(i2);
            outIndices.push_back(i3);
        }
    }
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  SIDERA Engine — Milestone 19: LOD Unit Tests      \n";
    std::cout << "====================================================\n\n";

    // -------------------------------------------------------------
    // Test 1: Single Mesh with One LOD Level
    // -------------------------------------------------------------
    {
        std::cout << "[Test 1] Single mesh LODGroup fallback...";
        LODGroup group("SingleLOD");
        group.addLevel(nullptr, 50.0f, 100.0f, 1.0f);
        TEST_ASSERT(group.getLevelCount() == 1, "Level count must be 1");

        LODConfig cfg{};
        cfg.selectionMode = LODSelectionMode::Distance;
        TEST_ASSERT(group.selectLevel(10.0f, 200.0f, 0, cfg) == 0, "Distance 10 should select LOD 0");
        TEST_ASSERT(group.selectLevel(100.0f, 10.0f, 0, cfg) == 0, "Distance 100 should still select LOD 0 (single tier)");
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 2: Multi-Tier LODGroup (4 Levels) Distance Selection
    // -------------------------------------------------------------
    {
        std::cout << "[Test 2] Distance-based 4-tier LOD selection...";
        LODGroup group("MultiLOD");
        group.addLevel(nullptr, 20.0f, 200.0f, 1.0f);  // LOD 0: 0 - 20
        group.addLevel(nullptr, 50.0f, 100.0f, 0.5f);  // LOD 1: 20 - 50
        group.addLevel(nullptr, 100.0f, 40.0f, 0.25f); // LOD 2: 50 - 100
        group.addLevel(nullptr, 1e6f, 0.0f, 0.1f);     // LOD 3: 100+

        LODConfig cfg{};
        cfg.selectionMode = LODSelectionMode::Distance;
        cfg.distanceHysteresis = 0.0f; // Pure threshold test

        TEST_ASSERT(group.selectLevel(10.0f, 0.0f, 0, cfg) == 0, "10 units should select LOD 0");
        TEST_ASSERT(group.selectLevel(35.0f, 0.0f, 0, cfg) == 1, "35 units should select LOD 1");
        TEST_ASSERT(group.selectLevel(75.0f, 0.0f, 0, cfg) == 2, "75 units should select LOD 2");
        TEST_ASSERT(group.selectLevel(150.0f, 0.0f, 0, cfg) == 3, "150 units should select LOD 3");
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 3: Distance Hysteresis (Deadband Prevention of Flutter)
    // -------------------------------------------------------------
    {
        std::cout << "[Test 3] Distance hysteresis deadband verification...";
        LODGroup group("HysteresisTest");
        group.addLevel(nullptr, 20.0f, 200.0f, 1.0f);  // LOD 0 threshold = 20.0
        group.addLevel(nullptr, 50.0f, 100.0f, 0.5f);  // LOD 1 threshold = 50.0
        group.addLevel(nullptr, 100.0f, 40.0f, 0.25f);
        group.addLevel(nullptr, 1e6f, 0.0f, 0.1f);

        LODConfig cfg{};
        cfg.selectionMode = LODSelectionMode::Distance;
        cfg.distanceHysteresis = 4.0f; // Hysteresis margin = 4.0 units

        // Currently at LOD 0, moving away across boundary (20.0):
        // Needs distance > 20.0 + 4.0 = 24.0 to step down to LOD 1
        TEST_ASSERT(group.selectLevel(21.0f, 0.0f, 0, cfg) == 0, "At 21 units from LOD 0, hysteresis must retain LOD 0");
        TEST_ASSERT(group.selectLevel(23.5f, 0.0f, 0, cfg) == 0, "At 23.5 units from LOD 0, hysteresis must retain LOD 0");
        TEST_ASSERT(group.selectLevel(24.5f, 0.0f, 0, cfg) == 1, "At 24.5 units from LOD 0, step down to LOD 1");

        // Currently at LOD 1, moving closer across boundary (20.0):
        // Needs distance <= 20.0 - 4.0 = 16.0 to step up to LOD 0
        TEST_ASSERT(group.selectLevel(18.0f, 0.0f, 1, cfg) == 1, "At 18 units from LOD 1, hysteresis must retain LOD 1");
        TEST_ASSERT(group.selectLevel(15.5f, 0.0f, 1, cfg) == 0, "At 15.5 units from LOD 1, step up to LOD 0");
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 4: Screen-Space LOD Selection
    // -------------------------------------------------------------
    {
        std::cout << "[Test 4] Screen-space pixel coverage LOD selection...";
        LODGroup group("ScreenLOD");
        group.addLevel(nullptr, 0.0f, 250.0f, 1.0f);  // LOD 0: >= 250 px
        group.addLevel(nullptr, 0.0f, 120.0f, 0.5f);  // LOD 1: 120 - 250 px
        group.addLevel(nullptr, 0.0f, 40.0f, 0.25f);  // LOD 2: 40 - 120 px
        group.addLevel(nullptr, 0.0f, 0.0f, 0.1f);    // LOD 3: < 40 px

        LODConfig cfg{};
        cfg.selectionMode = LODSelectionMode::ScreenSpace;
        cfg.screenHysteresis = 0.0f;

        TEST_ASSERT(group.selectLevel(0.0f, 320.0f, 0, cfg) == 0, "320 px should select LOD 0");
        TEST_ASSERT(group.selectLevel(0.0f, 180.0f, 0, cfg) == 1, "180 px should select LOD 1");
        TEST_ASSERT(group.selectLevel(0.0f, 65.0f, 0, cfg) == 2, "65 px should select LOD 2");
        TEST_ASSERT(group.selectLevel(0.0f, 20.0f, 0, cfg) == 3, "20 px should select LOD 3");
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 5: Forced LOD Overrides & Master Toggle
    // -------------------------------------------------------------
    {
        std::cout << "[Test 5] Forced LOD overrides & master disable toggle...";
        LODGroup group("OverrideTest");
        group.addLevel(nullptr, 20.0f);
        group.addLevel(nullptr, 50.0f);
        group.addLevel(nullptr, 100.0f);
        group.addLevel(nullptr, 1e6f);

        LODConfig cfg{};
        cfg.selectionMode = LODSelectionMode::Distance;

        // Force LOD 2 regardless of close distance
        cfg.forcedLOD = ForcedLODMode::ForceLOD2;
        TEST_ASSERT(group.selectLevel(5.0f, 0.0f, 0, cfg) == 2, "ForceLOD2 must return 2 even at close distance 5.0");

        // Force LOD 0 regardless of far distance
        cfg.forcedLOD = ForcedLODMode::ForceLOD0;
        TEST_ASSERT(group.selectLevel(500.0f, 0.0f, 0, cfg) == 0, "ForceLOD0 must return 0 even at far distance 500.0");

        // Master toggle disabled -> must always return 0
        cfg.forcedLOD = ForcedLODMode::Auto;
        cfg.enabled = false;
        TEST_ASSERT(group.selectLevel(500.0f, 0.0f, 0, cfg) == 0, "Disabled LOD must strictly return 0");
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 6: QEM Mesh Simplification (Ratio & Topology Validity)
    // -------------------------------------------------------------
    {
        std::cout << "[Test 6] QEM mesh simplification algorithm (50% reduction)...";
        std::vector<Vertex> sphereVerts;
        std::vector<uint32_t> sphereIndices;
        generateTestSphere(20, 30, 1.0f, sphereVerts, sphereIndices);
        const uint32_t origTris = static_cast<uint32_t>(sphereIndices.size() / 3);

        SimplificationOptions opts{};
        opts.targetRatio = 0.50f; // Request 50% triangle reduction
        opts.preserveBorder = true;
        opts.checkFaceFlipping = true;

        std::vector<Vertex> simpVerts;
        std::vector<uint32_t> simpIndices;
        const SimplificationResult res = MeshSimplifier::simplify(sphereVerts, sphereIndices, opts, simpVerts, simpIndices);

        TEST_ASSERT(res.success, "Simplification must report success");
        TEST_ASSERT(res.originalTriangles == origTris, "Original triangle count match");
        TEST_ASSERT(res.simplifiedTriangles < origTris, "Simplified triangle count must be lower");
        TEST_ASSERT(std::abs(res.achievedRatio - 0.50f) < 0.15f, "Achieved reduction ratio must be near 50%");
        TEST_ASSERT(!simpIndices.empty() && (simpIndices.size() % 3 == 0), "Index buffer must be valid multiple of 3");

        // Verify valid index bounds
        for (uint32_t idx : simpIndices) {
            TEST_ASSERT(idx < simpVerts.size(), "All indices must be within valid vertex range");
        }
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 7: Normal and UV Preservation during Simplification
    // -------------------------------------------------------------
    {
        std::cout << "[Test 7] Normal and UV coordinate sanity after decimation...";
        std::vector<Vertex> sphereVerts;
        std::vector<uint32_t> sphereIndices;
        generateTestSphere(16, 24, 1.0f, sphereVerts, sphereIndices);

        SimplificationOptions opts{};
        opts.targetRatio = 0.35f; // Request 65% reduction

        std::vector<Vertex> simpVerts;
        std::vector<uint32_t> simpIndices;
        const auto res = MeshSimplifier::simplify(sphereVerts, sphereIndices, opts, simpVerts, simpIndices);
        TEST_ASSERT(res.success, "Simplification succeeded");

        for (const auto& v : simpVerts) {
            TEST_ASSERT(!std::isnan(v.position.x) && !std::isnan(v.position.y) && !std::isnan(v.position.z), "No NaN positions");
            TEST_ASSERT(!std::isnan(v.normal.x) && !std::isnan(v.normal.y) && !std::isnan(v.normal.z), "No NaN normals");
            TEST_ASSERT(!std::isnan(v.uv.x) && !std::isnan(v.uv.y), "No NaN UVs");

            const float normalLen = glm::length(v.normal);
            TEST_ASSERT(std::abs(normalLen - 1.0f) < 0.05f, "Simplified vertex normals must be unit length");
            TEST_ASSERT(v.uv.x >= -0.01f && v.uv.x <= 1.01f, "UV.x in valid range");
            TEST_ASSERT(v.uv.y >= -0.01f && v.uv.y <= 1.01f, "UV.y in valid range");
        }
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 8: LOD Statistics Accounting
    // -------------------------------------------------------------
    {
        std::cout << "[Test 8] LODManager statistics calculation & reduction percent...";
        LODManager mgr;
        mgr.beginFrameStats();

        // Simulate 10 objects: 4 at LOD 0 (10k tris), 3 at LOD 1 (4k tris), 2 at LOD 2 (1k tris), 1 at LOD 3 (200 tris)
        for (int i = 0; i < 4; ++i) mgr.recordObjectLOD(0, 10000, 10000);
        for (int i = 0; i < 3; ++i) mgr.recordObjectLOD(1, 10000, 4000);
        for (int i = 0; i < 2; ++i) mgr.recordObjectLOD(2, 10000, 1000);
        for (int i = 0; i < 1; ++i) mgr.recordObjectLOD(3, 10000, 200);

        mgr.endFrameStats();
        const auto& stats = mgr.getStats();

        TEST_ASSERT(stats.totalLODObjects == 10, "Total objects must be 10");
        TEST_ASSERT(stats.lod0Count == 4, "LOD 0 count must be 4");
        TEST_ASSERT(stats.lod1Count == 3, "LOD 1 count must be 3");
        TEST_ASSERT(stats.lod2Count == 2, "LOD 2 count must be 2");
        TEST_ASSERT(stats.lod3Count == 1, "LOD 3 count must be 1");
        TEST_ASSERT(stats.originalTriangles == 100000, "Original triangles: 10 * 10,000 = 100,000");
        TEST_ASSERT(stats.totalLODTriangles == (40000 + 12000 + 2000 + 200), "Rendered triangles = 54,200");
        TEST_ASSERT(std::abs(stats.triangleReductionPercent - 45.8) < 0.1, "Reduction percent must be ~45.8%");
        std::cout << " PASSED\n";
    }

    std::cout << "\n====================================================\n";
    std::cout << "  ALL 8 LOD UNIT TESTS PASSED SUCCESSFULLY!         \n";
    std::cout << "====================================================\n";
    return 0;
}
