#pragma once

#include "renderer/Mesh.h"
#include "renderer/TextureManager.h"

#include <memory>
#include <string_view>

namespace sidera {

class VulkanDevice;

class ProceduralFoliage {
public:
    // Procedural Tree Trunk Mesh (Cylindrical tapered bark trunk, base at local Y = 0)
    [[nodiscard]] static std::unique_ptr<Mesh> createTreeTrunkMesh(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        uint32_t lodTier = 0,
        float trunkHeight = 4.0f,
        float baseRadius = 0.28f,
        float tipRadius = 0.12f,
        std::string_view name = "TreeTrunk"
    );

    // Procedural Tree Canopy Mesh (Conical / multi-tier fir or pine canopy with outward leaf normals)
    [[nodiscard]] static std::unique_ptr<Mesh> createTreeCanopyMesh(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        uint32_t lodTier = 0,
        float canopyBaseHeight = 1.8f,
        float canopyTopHeight = 6.2f,
        float maxRadius = 1.8f,
        std::string_view name = "TreeCanopy"
    );

    // Procedural Grass Clump Mesh (Intersecting curved, tapered grass blades rooted at Y = 0)
    [[nodiscard]] static std::unique_ptr<Mesh> createGrassClumpMesh(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        uint32_t bladeCount = 7,
        float clumpRadius = 0.45f,
        float bladeHeight = 0.85f,
        std::string_view name = "GrassClump"
    );

    // Procedural Bush Mesh (Clustered foliage domes, base at Y = 0)
    [[nodiscard]] static std::unique_ptr<Mesh> createBushMesh(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        uint32_t lodTier = 0,
        float radius = 0.95f,
        float height = 1.35f,
        std::string_view name = "Bush"
    );

    // Procedural Fern Fronds Mesh (Radial arching ground fronds rooted at Y = 0)
    [[nodiscard]] static std::unique_ptr<Mesh> createFernMesh(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        uint32_t frondCount = 6,
        float frondLength = 0.65f,
        std::string_view name = "Fern"
    );

    // Procedural Foliage Textures with alpha-test cutouts and normal maps
    static void createFoliageTextures(TextureManager& textureManager);
};

} // namespace sidera
