#pragma once

#include "lod/LODGroup.h"
#include "renderer/BoundingVolume.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"

#include <glm/vec3.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace sidera {

// High-level taxonomy of vegetative assets
enum class FoliageCategory : uint32_t {
    Tree = 0,
    Grass = 1,
    Bush = 2,
    SmallVegetation = 3
};

[[nodiscard]] constexpr std::string_view getFoliageCategoryName(FoliageCategory cat) noexcept {
    switch (cat) {
        case FoliageCategory::Tree:            return "Tree";
        case FoliageCategory::Grass:           return "Grass";
        case FoliageCategory::Bush:            return "Bush";
        case FoliageCategory::SmallVegetation: return "SmallVegetation";
    }
    return "Unknown";
}

// Blueprint definition for a species of foliage
struct FoliageType {
    uint32_t typeId = 0;                        // Unique integer identifier
    std::string name = "FoliageType";           // Human-readable asset name
    FoliageCategory category = FoliageCategory::Tree;

    // Geometric and visual asset references
    const Mesh* mesh = nullptr;                 // Base geometry (typically LOD 0)
    Material material{};                        // PBR material properties (albedo, roughness, alphaCutoff)
    LODGroup* lodGroup = nullptr;               // Optional multi-tier LOD container (Milestone 19)

    // Transform and scaling parameters
    glm::vec3 defaultScale{1.0f};               // Nominal scale factor
    glm::vec3 minScale{0.8f};                   // Minimum randomized scaling bound
    glm::vec3 maxScale{1.2f};                   // Maximum randomized scaling bound

    // Wind animation parameters
    float windBending = 1.0f;                   // Relative flexibility multiplier for this species
    float windFrequency = 1.0f;                 // Species-specific oscillation frequency multiplier

    // Lighting and shadow options
    bool castShadows = true;                    // Enable directional shadow casting in CSM cascades
    bool receiveShadows = true;                 // Receive shadows from sunlight and terrain

    // Terrain placement constraints
    bool alignToTerrainNormal = false;          // True = tilt with slope; False = stay vertical Y
    float maxSlopeAngleDegrees = 45.0f;         // Maximum terrain slope angle permitted for placement
    float minElevation = -100.0f;               // Minimum allowed world Y terrain altitude
    float maxElevation = 100.0f;                // Maximum allowed world Y terrain altitude
    float maxDistance = 160.0f;                 // Distance culling limit for this foliage species

    // Untransformed local bounding volumes
    AABB localBounds{glm::vec3(-0.5f, 0.0f, -0.5f), glm::vec3(0.5f, 2.0f, 0.5f)};
    BoundingSphere localSphere{glm::vec3(0.0f, 1.0f, 0.0f), 1.5f};
};

} // namespace sidera
