#pragma once

#include "renderer/BoundingVolume.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <cstdint>

namespace sidera {

// CPU-side instance representation within an InstanceBatch
struct Instance {
    glm::mat4 transform{1.0f};
    AABB worldAabb{};
    BoundingSphere worldSphere{};
    glm::vec3 position{0.0f};
    glm::vec3 rotationAxis{0.0f, 1.0f, 0.0f};
    float rotationRadians = 0.0f;
    float angularSpeed = 0.0f;
    uint32_t customId = 0;
};

// GPU-resident instance data (std430, 160 bytes, 16-byte aligned)
// Read by both the compute culling shader and the vertex shader via SSBO.
struct alignas(16) GpuInstanceData {
    glm::mat4 model{1.0f};                            // 64 bytes: World transform matrix
    glm::vec4 albedoMetallic{1.0f, 1.0f, 1.0f, 0.0f}; // 16 bytes: rgb = albedo tint, a = metallic
    glm::vec4 roughnessAoTex{0.5f, 1.0f, 0.0f, 0.0f}; // 16 bytes: x = roughness, y = ao, z = texture strength
    glm::uvec4 meshInfo{0u, 1u, 0u, 0u};              // 16 bytes: x = batchId, y = active, z = instanceId, w = pad
    glm::vec4 boundingSphere{0.0f, 0.0f, 0.0f, 1.0f}; // 16 bytes: xyz = world center, w = world radius
    glm::vec4 aabbMin{-1.0f, -1.0f, -1.0f, 0.0f};     // 16 bytes: xyz = world AABB min
    glm::vec4 aabbMax{1.0f, 1.0f, 1.0f, 0.0f};        // 16 bytes: xyz = world AABB max
};
static_assert(sizeof(GpuInstanceData) == 160, "GpuInstanceData must be exactly 160 bytes.");

// Per-batch information passed to compute shader to coordinate indirect draw generation
struct alignas(16) GpuBatchInfo {
    uint32_t meshIndex = 0;
    uint32_t firstInstance = 0;
    uint32_t instanceCount = 0;
    uint32_t pad = 0;
};
static_assert(sizeof(GpuBatchInfo) == 16, "GpuBatchInfo must be exactly 16 bytes.");

} // namespace sidera
