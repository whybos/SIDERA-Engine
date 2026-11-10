#pragma once

#include "foliage/FoliageSystem.h"
#include "renderer/Camera.h"
#include "renderer/InstanceBatch.h"

#include <memory>

namespace sidera {

class VulkanDevice;
class TextureManager;
class Terrain;

// High-level rendering orchestrator for the foliage subsystem
class FoliageRenderer {
public:
    explicit FoliageRenderer(FoliageSystem& system);
    ~FoliageRenderer() = default;

    FoliageRenderer(const FoliageRenderer&) = delete;
    FoliageRenderer& operator=(const FoliageRenderer&) = delete;
    FoliageRenderer(FoliageRenderer&&) noexcept = default;
    FoliageRenderer& operator=(FoliageRenderer&&) noexcept = default;

    // Creates the demo foliage ecosystem (trees, grass fields, bushes, small vegetation) on the terrain
    void initDemoScene(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        TextureManager& textureManager,
        LODManager& lodManager,
        const Terrain& terrain
    );

    // Updates visibility, culling and LOD tiers, and syncs batches. Returns true if visibility or LOD changed.
    bool update(float deltaTime, const Camera& camera, float viewportHeight, const LODManager& lodManager);

    // Syncs visible foliage instances into the main instanced batch manager (Milestone 16)
    void populateBatches(InstanceBatchManager& batchManager) const;

    [[nodiscard]] FoliageSystem& getSystem() noexcept { return m_system; }
    [[nodiscard]] const FoliageSystem& getSystem() const noexcept { return m_system; }

private:
    FoliageSystem& m_system;

    // Cached shared meshes
    std::unique_ptr<Mesh> m_treeTrunkLODs[4];
    std::unique_ptr<Mesh> m_treeCanopyLODs[4];
    std::unique_ptr<Mesh> m_grassClumpMesh;
    std::unique_ptr<Mesh> m_bushLODs[4];
    std::unique_ptr<Mesh> m_fernMesh;

    // Registered LOD groups
    LODGroup* m_treeCanopyLODGroup = nullptr;
    LODGroup* m_treeTrunkLODGroup = nullptr;
    LODGroup* m_bushLODGroup = nullptr;
};

} // namespace sidera
