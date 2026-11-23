#include "scene/Scene.h"
#include "scene/SceneSerializer.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace sidera;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { ++g_passed; } \
        else { ++g_failed; std::cerr << "[FAIL] " << msg << "\n"; } \
    } while (0)

static void testHierarchyAndValidation() {
    Scene s("TestScene");
    const EntityID root = s.createEntity("Root");
    const EntityID child = s.createEntity("Child");
    CHECK(root != kInvalidEntityID && child != kInvalidEntityID && root != child, "Unique EntityIDs assigned");
    CHECK(s.createEntity("Dup", root) == kInvalidEntityID, "Duplicate explicit EntityID rejected");
    CHECK(s.setParent(child, root), "Valid parent-child link set");
    CHECK(!s.setParent(root, child), "Hierarchy cycle rejected");
    CHECK(!s.setParent(root, root), "Self-parenting rejected");

    s.findEntity(root)->transform.position = glm::vec3(10.0f, 0.0f, 0.0f);
    s.findEntity(child)->transform.position = glm::vec3(0.0f, 5.0f, 0.0f);
    const glm::mat4 world = s.computeWorldMatrix(child);
    CHECK(std::abs(world[3].x - 10.0f) < 1e-4f && std::abs(world[3].y - 5.0f) < 1e-4f,
          "World matrix reconstructs parent + child transforms");
}

static void testTransactionalSaveLoadAndVersioning() {
    const std::filesystem::path tmpDir = "scene_serialization_unit_test_tmp";
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);

    AssetDatabase db;
    AssetMetadata meshMeta{};
    meshMeta.type = AssetType::Mesh;
    meshMeta.relativeSourcePath = "meshes/pillar.obj";
    const AssetGUID meshGuid = db.registerAsset(meshMeta);

    Scene orig("OpenWorldValley");
    const EntityID e1 = orig.createEntity("PillarRoot");
    const EntityID e2 = orig.createEntity("TorchChild");
    orig.setParent(e2, e1);
    orig.findEntity(e1)->meshAssetGuid = meshGuid;
    orig.findEntity(e1)->transform.position = glm::vec3(12.5f, 3.0f, -7.25f);
    orig.findEntity(e2)->components.push_back({"PointLight", 1, {{"intensity", "8.5"}, {"radius", "16.0"}}});
    orig.findEntity(e2)->components.push_back({"UnknownFutureComponent", 1, {{"flag", "true"}}});

    CHECK(orig.validate(&db).empty(), "Scene validates cleanly against AssetDatabase");
    CHECK(orig.collectAssetDependencies().size() == 1u, "Scene collects AssetGUID dependencies");

    const auto scenePath = tmpDir / "valley.sscene";
    CHECK(SceneSerializer::saveScene(orig, scenePath).success, "Transactional saveScene succeeds");

    Scene loaded;
    CHECK(SceneSerializer::loadScene(scenePath, loaded).success, "Transactional loadScene succeeds");
    CHECK(loaded.getGUID() == orig.getGUID() && loaded.getName() == orig.getName(), "Scene metadata preserved");
    CHECK(loaded.getEntityCount() == 2u, "Entity count preserved");
    CHECK(loaded.findEntity(e1)->meshAssetGuid == meshGuid, "AssetGUID reference preserved");
    CHECK(loaded.findEntity(e2)->parentId == e1, "Parent-child link preserved");
    CHECK(loaded.findEntity(e2)->components.size() == 2u, "Built-in and unknown components preserved");

    // Corrupt / unsupported version file must NOT corrupt existing loaded scene
    const auto badPath = tmpDir / "bad.sscene";
    {
        std::ofstream f(badPath);
        f << "SIDERA_SCENE\nschema_version 999\n";
    }
    CHECK(!SceneSerializer::loadScene(badPath, loaded).success, "Unsupported schema version rejected");
    CHECK(loaded.getEntityCount() == 2u && loaded.getName() == "OpenWorldValley",
          "Failed load preserves previous active scene transactionally");

    std::filesystem::remove_all(tmpDir, ec);
}

static void testPrefabInstantiationAndOverrides() {
    const std::filesystem::path tmpDir = "prefab_unit_test_tmp";
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);
    std::filesystem::create_directories(tmpDir, ec);

    Prefab pf{};
    pf.guid = AssetGUID::generate();
    pf.name = "LampPost";
    pf.rootEntityId = 1;
    SceneEntity post{}; post.id = 1; post.name = "Post"; post.transform.position = glm::vec3(0, 0, 0);
    SceneEntity bulb{}; bulb.id = 2; bulb.name = "Bulb"; bulb.parentId = 1; bulb.transform.position = glm::vec3(0, 4, 0);
    bulb.components.push_back({"PointLight", 1, {{"intensity", "5.0"}}});
    pf.entities[1] = post;
    pf.entities[2] = bulb;

    const auto pfPath = tmpDir / "lamppost.sprefab";
    CHECK(SceneSerializer::savePrefab(pf, pfPath).success, "savePrefab succeeds");
    Prefab loadedPf{};
    CHECK(SceneSerializer::loadPrefab(pfPath, loadedPf).success, "loadPrefab succeeds");

    Scene scene("TownSquare");
    const EntityID instA = SceneSerializer::instantiatePrefab(scene, loadedPf, glm::vec3(10, 0, 0));
    const EntityID instB = SceneSerializer::instantiatePrefab(
        scene, loadedPf, glm::vec3(20, 0, 0),
        {{1, "Entity", "active", "false"}});

    CHECK(instA != kInvalidEntityID && instB != kInvalidEntityID && instA != instB,
          "Multiple prefab instances receive unique EntityIDs");
    CHECK(scene.getEntityCount() == 4u, "2 instances x 2 entities == 4 entities");
    CHECK(scene.findEntity(instA)->active && !scene.findEntity(instB)->active, "Prefab override applied to instB only");
    CHECK(SceneSerializer::revertOverride(*scene.findEntity(instB), "Entity", "active"), "Individual override reverted");
    SceneSerializer::revertAllOverrides(*scene.findEntity(instB), loadedPf.entities.at(1));
    CHECK(scene.findEntity(instB)->active, "revertAllOverrides restores source prefab state");

    std::filesystem::remove_all(tmpDir, ec);
}

int main() {
    testHierarchyAndValidation();
    testTransactionalSaveLoadAndVersioning();
    testPrefabInstantiationAndOverrides();

    std::cout << "test_scene_serialization: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
