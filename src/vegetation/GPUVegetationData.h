#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <cstdint>

namespace sidera {

// std430-aligned GPU vegetation instance (112 bytes)
struct alignas(16) GPUVegetationInstance {
    glm::mat4 transform{1.0f};
    glm::vec4 boundsSphere{0.0f, 0.0f, 0.0f, 1.0f}; // xyz = center, w = radius
    glm::vec4 meta0{0.0f, 1.0f, 0.0f, 0.5f};        // x = speciesIdx, y = foliageTypeId, z = windPhase, w = windWeight
    glm::vec4 meta1{0.0f, 0.0f, 0.0f, 1.0f};        // x = randomSeed, y = flags, z = ownerCellLow, w = lodBias
};
static_assert(sizeof(GPUVegetationInstance) == 112, "GPUVegetationInstance must be 112 bytes (std430).");

// std430-aligned per-species LOD & wind metadata (48 bytes)
struct alignas(16) GPUVegetationSpeciesLOD {
    glm::vec4 lodDistances{20.0f, 45.0f, 85.0f, 160.0f}; // lod0Max, lod1Max, lod2Max, maxDrawDist
    glm::uvec4 lodBatchIndex{0u, 1u, 2u, 3u};            // indirect command batch index per LOD 0..3
    glm::vec4 windParams{0.45f, 1.25f, 0.85f, 1.0f};     // strength, speed, freq, castShadow
};
static_assert(sizeof(GPUVegetationSpeciesLOD) == 48, "GPUVegetationSpeciesLOD must be 48 bytes (std430).");

// Exact layout match for VkDrawIndexedIndirectCommand (20 bytes)
struct GPUVegetationIndirectCmd {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex = 0;
    int32_t  vertexOffset = 0;
    uint32_t firstInstance = 0;
};
static_assert(sizeof(GPUVegetationIndirectCmd) == 20, "GPUVegetationIndirectCmd must match VkDrawIndexedIndirectCommand.");

// std430-aligned atomic counter block (32 bytes)
struct alignas(16) GPUVegetationCounters {
    uint32_t totalVisible = 0;
    uint32_t totalCulled = 0;
    uint32_t lod0Count = 0;
    uint32_t lod1Count = 0;
    uint32_t lod2Count = 0;
    uint32_t lod3Count = 0;
    uint32_t overflowCount = 0;
    uint32_t pad0 = 0;
};
static_assert(sizeof(GPUVegetationCounters) == 32, "GPUVegetationCounters must be 32 bytes.");

// 128-byte push constants for veg_cull_compact.comp
struct alignas(16) GPUVegetationCullPC {
    glm::vec4 frustumPlanes[6]{};
    glm::vec4 cameraPosMaxDist{0.0f, 0.0f, 0.0f, 200.0f};
    glm::uvec4 countsFlags{0u, 1u, 3u, 65536u}; // x=instCount, y=speciesCount, z=flags, w=slotCapacity
};
static_assert(sizeof(GPUVegetationCullPC) == 128, "GPUVegetationCullPC must be 128 bytes.");

struct GPUVegetationConfig {
    uint32_t initialCapacity = 4096;
    uint32_t maxCapacity = 1000000;
    uint32_t maxBatches = 32;
    float globalMaxDrawDistance = 200.0f;
    bool frustumCullingEnabled = true;
    bool distanceCullingEnabled = true;
    bool occlusionCullingEnabled = false;
    bool windEnabled = true;
};

struct GPUVegetationStatistics {
    uint64_t totalInstances = 0;
    uint64_t visibleInstances = 0;
    uint64_t culledInstances = 0;
    uint32_t totalBatches = 0;
    uint32_t indirectDrawCommands = 0;
    uint32_t computeDispatchCount = 0;

    uint64_t lod0Count = 0;
    uint64_t lod1Count = 0;
    uint64_t lod2Count = 0;
    uint64_t lod3Count = 0;
    uint64_t overflowCount = 0;

    uint64_t visibleTriangles = 0;
    uint64_t instanceBufferMemoryBytes = 0;
    uint64_t totalGpuMemoryBytes = 0;
    uint32_t currentCapacity = 0;
    float cullDispatchTimeMs = 0.0f;
};

} // namespace sidera
