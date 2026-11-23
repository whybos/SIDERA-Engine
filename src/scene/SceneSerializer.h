#pragma once

#include "scene/Scene.h"

#include <filesystem>
#include <string>
#include <vector>

namespace sidera {

struct SceneIOStatus {
    bool success = false;
    std::string errorMessage;
    std::vector<std::string> warnings;
};

// Transactional Scene & Prefab Serializer with deterministic formatting,
// schema versioning, Prefab instantiation, and instance override management (Milestone 36).
class SceneSerializer {
public:
    // Transactional save (writes to temporary file, validates, then atomically replaces target)
    static SceneIOStatus saveScene(const Scene& scene, const std::filesystem::path& path);

    // Transactional load (parses & validates into temporary Scene; preserves active scene on failure)
    static SceneIOStatus loadScene(const std::filesystem::path& path, Scene& outScene);

    // Prefab save / load / instantiate / overrides
    static SceneIOStatus savePrefab(const Prefab& prefab, const std::filesystem::path& path);
    static SceneIOStatus loadPrefab(const std::filesystem::path& path, Prefab& outPrefab);

    // Instantiates prefab into scene with unique EntityIDs, remapped parent links, and applied overrides
    static EntityID instantiatePrefab(Scene& scene,
                                      const Prefab& prefab,
                                      const glm::vec3& worldOffset = glm::vec3(0.0f),
                                      const std::vector<PrefabOverride>& overrides = {});

    static void applyOverrides(SceneEntity& entity, const std::vector<PrefabOverride>& overrides);
    static bool revertOverride(SceneEntity& entity, const std::string& componentType, const std::string& propertyKey);
    static void revertAllOverrides(SceneEntity& entity, const SceneEntity& sourcePrefabEntity);
};

} // namespace sidera
