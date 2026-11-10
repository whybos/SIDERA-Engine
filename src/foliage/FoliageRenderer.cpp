#include "foliage/FoliageRenderer.h"
#include "core/Log.h"
#include "foliage/ProceduralFoliage.h"
#include "terrain/Terrain.h"

namespace sidera {

FoliageRenderer::FoliageRenderer(FoliageSystem& system)
    : m_system(system) {
}

void FoliageRenderer::initDemoScene(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    TextureManager& textureManager,
    LODManager& lodManager,
    const Terrain& terrain
) {
    SIDERA_LOG_INFO("Foliage", "Initializing procedural foliage ecosystem (Milestone 20)...");

    // 1. Procedural textures (Bark, Leaf cutout, Grass cutout)
    ProceduralFoliage::createFoliageTextures(textureManager);

    // 2. Multi-tier Tree Meshes (Canopy & Trunk across LOD 0..3)
    for (uint32_t lod = 0; lod < 4; ++lod) {
        m_treeCanopyLODs[lod] = ProceduralFoliage::createTreeCanopyMesh(
            device, transferCommandPool, lod, 1.8f, 6.2f, 1.85f,
            std::format("TreeCanopy_LOD{}", lod)
        );
        m_treeTrunkLODs[lod] = ProceduralFoliage::createTreeTrunkMesh(
            device, transferCommandPool, lod, 4.0f, 0.28f, 0.12f,
            std::format("TreeTrunk_LOD{}", lod)
        );
    }

    // 3. Register Tree Canopy LODGroup
    m_treeCanopyLODGroup = &lodManager.createLODGroup("TreeCanopyGroup");
    m_treeCanopyLODGroup->addLevel(m_treeCanopyLODs[0].get(), 25.0f, 200.0f, 1.0f);
    m_treeCanopyLODGroup->addLevel(m_treeCanopyLODs[1].get(), 50.0f, 90.0f, 0.50f);
    m_treeCanopyLODGroup->addLevel(m_treeCanopyLODs[2].get(), 85.0f, 40.0f, 0.25f);
    m_treeCanopyLODGroup->addLevel(m_treeCanopyLODs[3].get(), 1e6f, 0.0f, 0.10f);

    // 4. Multi-tier Bush Meshes across LOD 0..3
    for (uint32_t lod = 0; lod < 4; ++lod) {
        m_bushLODs[lod] = ProceduralFoliage::createBushMesh(
            device, transferCommandPool, lod, 1.05f, 1.40f,
            std::format("Bush_LOD{}", lod)
        );
    }

    // 5. Register Bush LODGroup
    m_bushLODGroup = &lodManager.createLODGroup("BushGroup");
    m_bushLODGroup->addLevel(m_bushLODs[0].get(), 22.0f, 180.0f, 1.0f);
    m_bushLODGroup->addLevel(m_bushLODs[1].get(), 45.0f, 75.0f, 0.50f);
    m_bushLODGroup->addLevel(m_bushLODs[2].get(), 75.0f, 30.0f, 0.25f);
    m_bushLODGroup->addLevel(m_bushLODs[3].get(), 1e6f, 0.0f, 0.10f);

    // 6. Grass Clump & Fern Meshes
    m_grassClumpMesh = ProceduralFoliage::createGrassClumpMesh(device, transferCommandPool, 7, 0.45f, 0.85f, "GrassClump");
    m_fernMesh = ProceduralFoliage::createFernMesh(device, transferCommandPool, 6, 0.65f, "FernFronds");

    // 7. Register Foliage Types
    // Type 1: Conifer / Pine Tree
    FoliageType treeType{};
    treeType.name = "PineTree";
    treeType.category = FoliageCategory::Tree;
    treeType.mesh = m_treeCanopyLODs[0].get();
    treeType.lodGroup = m_treeCanopyLODGroup;
    treeType.material = Material{
        .albedo = glm::vec3(0.20f, 0.44f, 0.16f),
        .metallic = 0.0f,
        .roughness = 0.65f,
        .ao = 1.0f,
        .textureStrength = 1.0f,
        .normalMapStrength = 1.0f,
        .alphaCutoff = 0.5f,
        .flags = 1u // wind animation
    };
    treeType.defaultScale = glm::vec3(1.0f, 1.0f, 1.0f);
    treeType.minScale = glm::vec3(0.85f, 0.85f, 0.85f);
    treeType.maxScale = glm::vec3(1.30f, 1.35f, 1.30f);
    treeType.windBending = 1.0f;
    treeType.castShadows = true;
    treeType.receiveShadows = true;
    treeType.alignToTerrainNormal = false; // Trees grow straight vertical
    treeType.maxSlopeAngleDegrees = 38.0f;
    treeType.localBounds = AABB{glm::vec3(-1.85f, 0.0f, -1.85f), glm::vec3(1.85f, 6.2f, 1.85f)};
    treeType.localSphere = BoundingSphere{glm::vec3(0.0f, 3.5f, 0.0f), 3.8f};
    const uint32_t treeTypeId = m_system.registerType(treeType);

    // Type 2: Lush Bush / Shrub
    FoliageType bushType{};
    bushType.name = "BroadleafBush";
    bushType.category = FoliageCategory::Bush;
    bushType.mesh = m_bushLODs[0].get();
    bushType.lodGroup = m_bushLODGroup;
    bushType.material = Material{
        .albedo = glm::vec3(0.28f, 0.58f, 0.18f),
        .metallic = 0.0f,
        .roughness = 0.60f,
        .ao = 1.0f,
        .textureStrength = 1.0f,
        .normalMapStrength = 1.0f,
        .alphaCutoff = 0.5f,
        .flags = 1u
    };
    bushType.defaultScale = glm::vec3(1.0f, 1.0f, 1.0f);
    bushType.minScale = glm::vec3(0.70f, 0.70f, 0.70f);
    bushType.maxScale = glm::vec3(1.25f, 1.25f, 1.25f);
    bushType.windBending = 1.2f;
    bushType.castShadows = true;
    bushType.receiveShadows = true;
    bushType.alignToTerrainNormal = true;
    bushType.maxSlopeAngleDegrees = 45.0f;
    bushType.localBounds = AABB{glm::vec3(-1.05f, 0.0f, -1.05f), glm::vec3(1.05f, 1.4f, 1.05f)};
    bushType.localSphere = BoundingSphere{glm::vec3(0.0f, 0.7f, 0.0f), 1.2f};
    const uint32_t bushTypeId = m_system.registerType(bushType);

    // Type 3: Grass Meadow
    FoliageType grassType{};
    grassType.name = "MeadowGrass";
    grassType.category = FoliageCategory::Grass;
    grassType.mesh = m_grassClumpMesh.get();
    grassType.lodGroup = nullptr;
    grassType.material = Material{
        .albedo = glm::vec3(0.38f, 0.72f, 0.20f),
        .metallic = 0.0f,
        .roughness = 0.70f,
        .ao = 1.0f,
        .textureStrength = 1.0f,
        .normalMapStrength = 1.0f,
        .alphaCutoff = 0.5f,
        .flags = 1u
    };
    grassType.defaultScale = glm::vec3(1.0f, 1.0f, 1.0f);
    grassType.minScale = glm::vec3(0.75f, 0.75f, 0.75f);
    grassType.maxScale = glm::vec3(1.35f, 1.40f, 1.35f);
    grassType.windBending = 1.8f;
    grassType.castShadows = false; // Grass shadow disabled for optimal fill-rate
    grassType.receiveShadows = true;
    grassType.alignToTerrainNormal = true;
    grassType.maxSlopeAngleDegrees = 45.0f;
    grassType.localBounds = AABB{glm::vec3(-0.45f, 0.0f, -0.45f), glm::vec3(0.45f, 0.85f, 0.45f)};
    grassType.localSphere = BoundingSphere{glm::vec3(0.0f, 0.42f, 0.0f), 0.65f};
    const uint32_t grassTypeId = m_system.registerType(grassType);

    // Type 4: Small Vegetation / Fern
    FoliageType fernType{};
    fernType.name = "GroundFern";
    fernType.category = FoliageCategory::SmallVegetation;
    fernType.mesh = m_fernMesh.get();
    fernType.lodGroup = nullptr;
    fernType.material = Material{
        .albedo = glm::vec3(0.24f, 0.52f, 0.15f),
        .metallic = 0.0f,
        .roughness = 0.55f,
        .ao = 1.0f,
        .textureStrength = 1.0f,
        .normalMapStrength = 1.0f,
        .alphaCutoff = 0.5f,
        .flags = 1u
    };
    fernType.defaultScale = glm::vec3(1.0f, 1.0f, 1.0f);
    fernType.minScale = glm::vec3(0.65f, 0.65f, 0.65f);
    fernType.maxScale = glm::vec3(1.15f, 1.15f, 1.15f);
    fernType.windBending = 1.4f;
    fernType.castShadows = false;
    fernType.receiveShadows = true;
    fernType.alignToTerrainNormal = true;
    fernType.maxSlopeAngleDegrees = 42.0f;
    fernType.localBounds = AABB{glm::vec3(-0.65f, 0.0f, -0.65f), glm::vec3(0.65f, 0.35f, 0.65f)};
    fernType.localSphere = BoundingSphere{glm::vec3(0.0f, 0.2f, 0.0f), 0.7f};
    const uint32_t fernTypeId = m_system.registerType(fernType);

    // 8. Deterministic Ecosystem Placement on Terrain (Milestone 20 Demo)
    // A. Specimen trees placed in close-range observation area (Z in [-14, -6], X in [-16, 16])
    m_system.addInstance(treeTypeId, -12.5f, -8.0f, terrain, 101);
    m_system.addInstance(treeTypeId, 12.5f, -8.0f, terrain, 102);
    m_system.addInstance(treeTypeId, -6.5f, -14.0f, terrain, 103);
    m_system.addInstance(treeTypeId, 6.5f, -14.0f, terrain, 104);

    // B. West Forest Grove (35 trees)
    m_system.generateFoliageField(treeTypeId, 35, -45.0f, -18.0f, -50.0f, -12.0f, terrain, 2024);

    // C. East Forest Grove (35 trees)
    m_system.generateFoliageField(treeTypeId, 35, 18.0f, 45.0f, -50.0f, -12.0f, terrain, 2025);

    // D. Ridge Crest Trees (15 trees)
    m_system.generateFoliageField(treeTypeId, 15, -25.0f, 25.0f, -54.0f, -38.0f, terrain, 2026);

    // E. Bush Clusters (65 bushes along grove borders and valley edges)
    m_system.generateFoliageField(bushTypeId, 30, -38.0f, -10.0f, -35.0f, -5.0f, terrain, 3001);
    m_system.generateFoliageField(bushTypeId, 35, 10.0f, 38.0f, -35.0f, -5.0f, terrain, 3002);

    // F. Grass Meadows (350 grass clumps across central valley and slopes)
    m_system.generateFoliageField(grassTypeId, 350, -35.0f, 35.0f, -42.0f, 8.0f, terrain, 4001);

    // G. Ground Ferns / Small Vegetation (60 instances nestled near trees)
    m_system.generateFoliageField(fernTypeId, 30, -32.0f, -8.0f, -30.0f, -2.0f, terrain, 5001);
    m_system.generateFoliageField(fernTypeId, 30, 8.0f, 32.0f, -30.0f, -2.0f, terrain, 5002);

    SIDERA_LOG_INFO(
        "Foliage",
        "Foliage ecosystem initialized: {} total instances across {} types.",
        m_system.getInstanceCount(),
        m_system.getTypes().size()
    );
}

bool FoliageRenderer::update(float deltaTime, const Camera& camera, float viewportHeight, const LODManager& lodManager) {
    return m_system.update(deltaTime, camera, viewportHeight, lodManager);
}

void FoliageRenderer::populateBatches(InstanceBatchManager& batchManager) const {
    m_system.populateBatches(batchManager);
}

} // namespace sidera
