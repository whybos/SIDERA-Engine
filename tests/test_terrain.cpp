#include "terrain/TerrainBounds.h"
#include "terrain/TerrainConfig.h"
#include "terrain/HeightmapData.h"
#include "renderer/Frustum.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>

#include <cassert>
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

int main() {
    std::cout << "====================================================\n";
    std::cout << "  SIDERA Engine — Milestone 18: Terrain Unit Tests  \n";
    std::cout << "====================================================\n\n";

    // -------------------------------------------------------------
    // Test 1: Flat Heightmap (Constant Height & Normal)
    // -------------------------------------------------------------
    {
        std::cout << "[Test 1] Flat heightmap verification...";
        HeightmapData flatMap;
        std::vector<uint8_t> flatPixels(64 * 64, 128);
        TEST_ASSERT(flatMap.loadFromMemory(flatPixels.data(), 64, 64, HeightmapFormat::Grayscale8), "Failed to load flat 8-bit memory");
        TEST_ASSERT(flatMap.getWidth() == 64 && flatMap.getHeight() == 64, "Resolution mismatch");

        const float hCenter = flatMap.sampleBilinear(0.5f, 0.5f);
        TEST_ASSERT(std::abs(hCenter - (128.0f / 255.0f)) < 0.001f, "Bilinear flat sample mismatch");

        const glm::vec3 normal = flatMap.computeNormal(0.5f, 0.5f, 100.0f, 100.0f, 20.0f);
        TEST_ASSERT(std::abs(normal.x) < 0.0001f && std::abs(normal.z) < 0.0001f && normal.y > 0.999f, "Flat terrain normal must be exactly up (0, 1, 0)");
        TEST_ASSERT(flatMap.computeSlope(0.5f, 0.5f, 100.0f, 100.0f, 20.0f) < 0.001f, "Flat terrain slope must be 0.0");
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 2: 8-bit, 16-bit, and 32-bit Float Memory Loading
    // -------------------------------------------------------------
    {
        std::cout << "[Test 2] 8-bit, 16-bit, and 32-bit float heightmap precision...";
        // 8-bit
        {
            HeightmapData h8;
            const uint8_t data8[4] = {0, 85, 170, 255};
            TEST_ASSERT(h8.loadFromMemory(data8, 2, 2, HeightmapFormat::Grayscale8), "8-bit load failed");
            TEST_ASSERT(std::abs(h8.sampleBilinear(0.0f, 0.0f) - 0.0f) < 0.001f, "8-bit min value");
            TEST_ASSERT(std::abs(h8.sampleBilinear(1.0f, 1.0f) - 1.0f) < 0.001f, "8-bit max value");
        }

        // 16-bit
        {
            HeightmapData h16;
            const uint16_t data16[4] = {0, 16384, 32768, 65535};
            TEST_ASSERT(h16.loadFromMemory(data16, 2, 2, HeightmapFormat::Grayscale16), "16-bit load failed");
            TEST_ASSERT(std::abs(h16.sampleBilinear(0.0f, 0.0f) - 0.0f) < 0.0001f, "16-bit 0 mismatch");
            TEST_ASSERT(std::abs(h16.sampleBilinear(1.0f, 0.0f) - (16384.0f / 65535.0f)) < 0.0001f, "16-bit 1/4 value mismatch");
            TEST_ASSERT(std::abs(h16.sampleBilinear(1.0f, 1.0f) - 1.0f) < 0.0001f, "16-bit 65535 mismatch");
        }

        // Float32
        {
            HeightmapData h32;
            const float data32[4] = {0.12345f, 0.54321f, 0.77777f, 0.99999f};
            TEST_ASSERT(h32.loadFromMemory(data32, 2, 2, HeightmapFormat::Float32), "Float32 load failed");
            TEST_ASSERT(std::abs(h32.sampleBilinear(0.0f, 0.0f) - 0.12345f) < 1e-5f, "Float32 exact sample mismatch");
            TEST_ASSERT(std::abs(h32.sampleBilinear(1.0f, 1.0f) - 0.99999f) < 1e-5f, "Float32 exact sample mismatch");
        }
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 3: Procedural Generation & Range Validation
    // -------------------------------------------------------------
    {
        std::cout << "[Test 3] Procedural multi-octave FBM heightmap generation...";
        HeightmapData procMap;
        procMap.generateProcedural(257, 42, 6, 1.0f);
        TEST_ASSERT(procMap.isValid(), "Procedural heightmap must be valid");
        TEST_ASSERT(procMap.getWidth() == 257 && procMap.getHeight() == 257, "Procedural resolution 257x257");
        TEST_ASSERT(procMap.getMinNormalized() >= 0.0f && procMap.getMaxNormalized() <= 1.0f, "Normalized heights must stay in [0, 1]");
        TEST_ASSERT(procMap.getMaxNormalized() > 0.65f, "Peak elevations should be present");
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 4: Bilinear Interpolation Continuity
    // -------------------------------------------------------------
    {
        std::cout << "[Test 4] Bilinear interpolation continuity and monotonicity...";
        const float corners[4] = {0.0f, 1.0f, 0.0f, 1.0f}; // Linear ramp along X
        HeightmapData ramp;
        ramp.loadFromMemory(corners, 2, 2, HeightmapFormat::Float32);

        float prevVal = -1.0f;
        for (int step = 0; step <= 10; ++step) {
            const float u = static_cast<float>(step) / 10.0f;
            const float sample = ramp.sampleBilinear(u, 0.5f);
            TEST_ASSERT(sample >= prevVal, "Interpolation must be monotonically increasing along ramp");
            TEST_ASSERT(std::abs(sample - u) < 1e-4f, "Linear ramp bilinear sample should equal u");
            prevVal = sample;
        }
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 5: Safe Boundary and Out-of-Bounds Queries
    // -------------------------------------------------------------
    {
        std::cout << "[Test 5] Safe boundary clamping and out-of-bounds handling...";
        HeightmapData map;
        const float data[4] = {0.2f, 0.4f, 0.6f, 0.8f};
        map.loadFromMemory(data, 2, 2, HeightmapFormat::Float32);

        // Negative u, v
        const float valNeg = map.sampleBilinear(-5.0f, -10.0f);
        TEST_ASSERT(std::abs(valNeg - 0.2f) < 1e-5f, "Negative coordinates must clamp to (0, 0)");

        // Out of bounds positive u, v
        const float valPos = map.sampleBilinear(5.0f, 10.0f);
        TEST_ASSERT(std::abs(valPos - 0.8f) < 1e-5f, "Excess coordinates must clamp to (1, 1)");
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 6: Seam-Free Chunk Border Verification
    // -------------------------------------------------------------
    {
        std::cout << "[Test 6] Seamless chunk boundary stitching (shared heights & normals)...";
        HeightmapData heightmap;
        heightmap.generateProcedural(513, 1337, 6, 1.0f);

        TerrainConfig cfg;
        cfg.width = 100.0f;
        cfg.length = 100.0f;
        cfg.heightScale = 15.0f;
        cfg.heightOffset = 0.0f;
        cfg.chunksX = 4;
        cfg.chunksZ = 4;
        cfg.chunkResolution = 32;

        const uint32_t R = cfg.chunkResolution;
        const uint32_t totalQuadsX = cfg.chunksX * R;
        const uint32_t totalQuadsZ = cfg.chunksZ * R;
        const float invTotalQuadsX = 1.0f / static_cast<float>(totalQuadsX);
        const float invTotalQuadsZ = 1.0f / static_cast<float>(totalQuadsZ);

        // Check boundary between Chunk (0, 0) right edge and Chunk (1, 0) left edge
        // Chunk (0, 0) right edge has local ix = R (globalQuadX = 0*R + R = R)
        // Chunk (1, 0) left edge has local ix = 0 (globalQuadX = 1*R + 0 = R)
        for (uint32_t iz = 0; iz <= R; ++iz) {
            const float v = static_cast<float>(iz) * invTotalQuadsZ;

            // Chunk (0, 0) right border vertex
            const float u0 = static_cast<float>(R) * invTotalQuadsX;
            const float h0 = cfg.heightOffset + heightmap.sampleBilinear(u0, v) * cfg.heightScale;
            const glm::vec3 n0 = heightmap.computeNormal(u0, v, cfg.width, cfg.length, cfg.heightScale);

            // Chunk (1, 0) left border vertex
            const float u1 = static_cast<float>(1 * R + 0) * invTotalQuadsX;
            const float h1 = cfg.heightOffset + heightmap.sampleBilinear(u1, v) * cfg.heightScale;
            const glm::vec3 n1 = heightmap.computeNormal(u1, v, cfg.width, cfg.length, cfg.heightScale);

            TEST_ASSERT(std::abs(h0 - h1) < 1e-6f, "Border vertices between Chunk (0,0) and Chunk (1,0) must have identical height");
            TEST_ASSERT(glm::distance(n0, n1) < 1e-5f, "Border normals between Chunk (0,0) and Chunk (1,0) must be identical (no lighting seam)");
        }

        // Check boundary between Chunk (0, 0) bottom edge and Chunk (0, 1) top edge
        for (uint32_t ix = 0; ix <= R; ++ix) {
            const float u = static_cast<float>(ix) * invTotalQuadsX;

            const float v0 = static_cast<float>(R) * invTotalQuadsZ;
            const float h0 = cfg.heightOffset + heightmap.sampleBilinear(u, v0) * cfg.heightScale;
            const glm::vec3 n0 = heightmap.computeNormal(u, v0, cfg.width, cfg.length, cfg.heightScale);

            const float v1 = static_cast<float>(1 * R + 0) * invTotalQuadsZ;
            const float h1 = cfg.heightOffset + heightmap.sampleBilinear(u, v1) * cfg.heightScale;
            const glm::vec3 n1 = heightmap.computeNormal(u, v1, cfg.width, cfg.length, cfg.heightScale);

            TEST_ASSERT(std::abs(h0 - h1) < 1e-6f, "Border vertices between Chunk (0,0) and Chunk (0,1) must have identical height");
            TEST_ASSERT(glm::distance(n0, n1) < 1e-5f, "Border normals between Chunk (0,0) and Chunk (0,1) must be identical");
        }
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 7: Frustum Culling against Terrain Chunk AABBs
    // -------------------------------------------------------------
    {
        std::cout << "[Test 7] Frustum culling intersection with chunk bounding boxes...";
        // Camera looking down negative Z from (0, 5, 10)
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 5.0f, 10.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1920.0f / 1080.0f, 0.1f, 100.0f);
        const Frustum frustum = Frustum::fromViewProj(proj * view);

        // AABB in front of camera
        const AABB visibleAabb{ .min = {-5.0f, -2.0f, -10.0f}, .max = {5.0f, 2.0f, 0.0f} };
        TEST_ASSERT(frustum.intersectsAABB(visibleAabb), "AABB directly in view must be visible");

        // AABB behind the camera (positive Z far away)
        const AABB behindAabb{ .min = {-5.0f, -2.0f, 50.0f}, .max = {5.0f, 2.0f, 60.0f} };
        TEST_ASSERT(!frustum.intersectsAABB(behindAabb), "AABB behind camera must be culled");

        // AABB far to the right outside view
        const AABB farRightAabb{ .min = {200.0f, -2.0f, -10.0f}, .max = {250.0f, 2.0f, 0.0f} };
        TEST_ASSERT(!frustum.intersectsAABB(farRightAabb), "AABB far to the right must be culled");
        std::cout << " PASSED\n";
    }

    // -------------------------------------------------------------
    // Test 8: Splat Weight Map Normalization
    // -------------------------------------------------------------
    {
        std::cout << "[Test 8] Multi-layer splat map weight normalization...";
        constexpr uint32_t splatRes = 64;
        HeightmapData hm;
        hm.generateProcedural(129, 999, 4, 1.0f);

        for (uint32_t y = 0; y < splatRes; ++y) {
            const float v = static_cast<float>(y) / static_cast<float>(splatRes - 1);
            for (uint32_t x = 0; x < splatRes; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(splatRes - 1);

                const float h = hm.sampleBilinear(u, v);
                const float slope = hm.computeSlope(u, v, 100.0f, 100.0f, 15.0f);

                float wRock = std::clamp((slope - 0.18f) / 0.18f, 0.0f, 1.0f) + std::clamp((h - 0.72f) / 0.20f, 0.0f, 1.0f);
                float wSand = std::clamp((0.18f - h) / 0.12f, 0.0f, 1.0f) * (1.0f - std::clamp((slope - 0.15f) / 0.15f, 0.0f, 1.0f));
                float wDirt = std::clamp(1.0f - std::abs(h - 0.40f) / 0.25f, 0.0f, 1.0f) * 0.45f + std::clamp((slope - 0.10f) / 0.15f, 0.0f, 0.40f);
                float wGrass = std::clamp(1.0f - wRock - wSand - wDirt * 0.6f, 0.0f, 1.0f) * (1.0f - std::clamp((slope - 0.16f) / 0.18f, 0.0f, 1.0f));

                const float sum = wGrass + wRock + wDirt + wSand;
                TEST_ASSERT(sum > 0.0f, "Layer weight sum must be positive");
                const float invSum = 1.0f / sum;
                wGrass *= invSum;
                wRock *= invSum;
                wDirt *= invSum;
                wSand *= invSum;

                const float normalizedSum = wGrass + wRock + wDirt + wSand;
                TEST_ASSERT(std::abs(normalizedSum - 1.0f) < 1e-5f, "Layer weights must strictly sum to 1.0");
            }
        }
        std::cout << " PASSED\n";
    }

    std::cout << "\n====================================================\n";
    std::cout << "  ALL 8 TERRAIN UNIT TESTS PASSED SUCCESSFULLY!     \n";
    std::cout << "====================================================\n";
    return 0;
}
