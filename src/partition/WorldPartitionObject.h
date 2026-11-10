#pragma once

#include "renderer/BoundingVolume.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sidera {

enum class PartitionObjectType : uint8_t {
    StaticMesh = 0,
    RenderObject,
    TerrainChunk,
    FoliageGroup,
    Decoration,
    Environment
};

[[nodiscard]] constexpr std::string_view getPartitionObjectTypeName(PartitionObjectType type) noexcept {
    switch (type) {
        case PartitionObjectType::StaticMesh:   return "StaticMesh";
        case PartitionObjectType::RenderObject: return "RenderObject";
        case PartitionObjectType::TerrainChunk: return "TerrainChunk";
        case PartitionObjectType::FoliageGroup: return "FoliageGroup";
        case PartitionObjectType::Decoration:   return "Decoration";
        case PartitionObjectType::Environment:  return "Environment";
    }
    return "Unknown";
}

// Representation of a spatially registered object within the World Partition hierarchy
struct WorldPartitionObject {
    uint32_t id = 0;
    std::string name;
    PartitionObjectType type = PartitionObjectType::StaticMesh;
    glm::vec3 position{0.0f};
    AABB worldBounds{};

    // Canonical ownership: exactly one primary cell owns this object for lifecycle and rendering
    uint64_t ownerCellId = 0;

    // Secondary spatial references: other cells whose world bounds intersect worldBounds
    // Used exclusively for spatial queries (frustum, radius, raycast) to avoid duplicate rendering
    std::vector<uint64_t> overlappingCellIds;

    void* userPointer = nullptr;
    uint64_t memoryBytes = 0;
    bool active = true;

    [[nodiscard]] bool spansMultipleCells() const noexcept {
        return !overlappingCellIds.empty();
    }
};

} // namespace sidera
