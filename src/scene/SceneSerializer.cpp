#include "scene/SceneSerializer.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace sidera {

namespace {

std::string serializeSceneText(const Scene& scene) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "SIDERA_SCENE\n";
    out << "schema_version " << scene.getSchemaVersion() << "\n";
    out << "scene_guid " << scene.getGUID().toString() << "\n";
    out << "scene_name " << scene.getName() << "\n";

    for (const auto& [id, e] : scene.getEntities()) {
        out << "ENTITY " << e.id << "\n";
        out << "  name " << e.name << "\n";
        out << "  active " << (e.active ? 1 : 0) << "\n";
        out << "  parent " << e.parentId << "\n";
        out << "  pos " << e.transform.position.x << " " << e.transform.position.y << " " << e.transform.position.z << "\n";
        out << "  rot " << e.transform.rotationEulerDeg.x << " " << e.transform.rotationEulerDeg.y << " " << e.transform.rotationEulerDeg.z << "\n";
        out << "  scale " << e.transform.scale.x << " " << e.transform.scale.y << " " << e.transform.scale.z << "\n";
        if (e.meshAssetGuid.isValid()) out << "  mesh_guid " << e.meshAssetGuid.toString() << "\n";
        if (e.materialAssetGuid.isValid()) out << "  mat_guid " << e.materialAssetGuid.toString() << "\n";
        if (e.prefabSourceGuid.isValid()) out << "  prefab_guid " << e.prefabSourceGuid.toString() << "\n";
        for (const auto& c : e.components) {
            out << "  COMPONENT " << c.typeName << " " << c.version << "\n";
            for (const auto& [k, v] : c.properties) {
                out << "    prop " << k << " " << v << "\n";
            }
        }
        out << "END_ENTITY\n";
    }
    return out.str();
}

} // namespace

SceneIOStatus SceneSerializer::saveScene(const Scene& scene, const std::filesystem::path& path) {
    SceneIOStatus status{};
    const auto issues = scene.validate();
    for (const auto& i : issues) {
        if (i.isError) {
            status.errorMessage = "Scene validation failed before save: " + i.message;
            return status;
        }
    }

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    const std::filesystem::path tmpPath = path.string() + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            status.errorMessage = "Failed to open temporary scene file for writing.";
            return status;
        }
        const std::string data = serializeSceneText(scene);
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.flush();
        if (!f.good()) {
            status.errorMessage = "I/O error while writing temporary scene file.";
            return status;
        }
    }

    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmpPath, path, ec);
        if (ec) {
            status.errorMessage = "Failed to atomically replace destination scene file.";
            return status;
        }
    }

    status.success = true;
    return status;
}

SceneIOStatus SceneSerializer::loadScene(const std::filesystem::path& path, Scene& outScene) {
    SceneIOStatus status{};
    std::ifstream f(path);
    if (!f.is_open()) {
        status.errorMessage = "Failed to open scene file.";
        return status;
    }

    std::string header;
    if (!std::getline(f, header) || header != "SIDERA_SCENE") {
        status.errorMessage = "Invalid scene file header.";
        return status;
    }

    Scene temp;
    SceneEntity current{};
    bool inEntity = false;
    ComponentRecord* currentComp = nullptr;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string key;
        iss >> key;
        if (key == "schema_version") {
            uint32_t ver = 0;
            iss >> ver;
            if (ver == 0 || ver > Scene::kCurrentSchemaVersion) {
                status.errorMessage = "Unsupported scene schema version: " + std::to_string(ver);
                return status;
            }
            temp.setSchemaVersion(ver);
        } else if (key == "scene_guid") {
            std::string g;
            iss >> g;
            temp.setGUID(AssetGUID::fromString(g));
        } else if (key == "scene_name") {
            std::string n;
            std::getline(iss >> std::ws, n);
            temp.setName(n);
        } else if (key == "ENTITY") {
            current = {};
            iss >> current.id;
            inEntity = true;
            currentComp = nullptr;
        } else if (key == "END_ENTITY" && inEntity) {
            if (!temp.insertEntity(current)) {
                status.errorMessage = "Duplicate or invalid EntityID in scene file.";
                return status;
            }
            inEntity = false;
            currentComp = nullptr;
        } else if (inEntity) {
            if (key == "name") {
                std::getline(iss >> std::ws, current.name);
            } else if (key == "active") {
                int a = 1; iss >> a; current.active = (a != 0);
            } else if (key == "parent") {
                iss >> current.parentId;
            } else if (key == "pos") {
                iss >> current.transform.position.x >> current.transform.position.y >> current.transform.position.z;
            } else if (key == "rot") {
                iss >> current.transform.rotationEulerDeg.x >> current.transform.rotationEulerDeg.y >> current.transform.rotationEulerDeg.z;
            } else if (key == "scale") {
                iss >> current.transform.scale.x >> current.transform.scale.y >> current.transform.scale.z;
            } else if (key == "mesh_guid") {
                std::string g; iss >> g; current.meshAssetGuid = AssetGUID::fromString(g);
            } else if (key == "mat_guid") {
                std::string g; iss >> g; current.materialAssetGuid = AssetGUID::fromString(g);
            } else if (key == "prefab_guid") {
                std::string g; iss >> g; current.prefabSourceGuid = AssetGUID::fromString(g);
            } else if (key == "COMPONENT") {
                ComponentRecord comp{};
                iss >> comp.typeName >> comp.version;
                current.components.push_back(std::move(comp));
                currentComp = &current.components.back();
            } else if (key == "prop" && currentComp) {
                std::string pk, pv;
                iss >> pk;
                std::getline(iss >> std::ws, pv);
                currentComp->properties[pk] = pv;
            }
        }
    }

    const auto issues = temp.validate();
    for (const auto& i : issues) {
        if (i.isError) {
            status.errorMessage = "Loaded scene failed validation: " + i.message;
            return status;
        }
    }

    // Transactional commit: only replace outScene after full validation succeeds
    outScene = std::move(temp);
    status.success = true;
    return status;
}

SceneIOStatus SceneSerializer::savePrefab(const Prefab& prefab, const std::filesystem::path& path) {
    Scene s(prefab.name);
    s.setGUID(prefab.guid.isValid() ? prefab.guid : AssetGUID::generate());
    s.setSchemaVersion(prefab.schemaVersion);
    for (const auto& [id, e] : prefab.entities) {
        s.insertEntity(e);
    }
    return saveScene(s, path);
}

SceneIOStatus SceneSerializer::loadPrefab(const std::filesystem::path& path, Prefab& outPrefab) {
    Scene temp;
    const SceneIOStatus st = loadScene(path, temp);
    if (!st.success) return st;
    Prefab p{};
    p.guid = temp.getGUID();
    p.name = temp.getName();
    p.schemaVersion = temp.getSchemaVersion();
    p.entities = temp.getEntities();
    for (const auto& [id, e] : p.entities) {
        if (e.parentId == kInvalidEntityID) {
            p.rootEntityId = id;
            break;
        }
    }
    outPrefab = std::move(p);
    return st;
}

EntityID SceneSerializer::instantiatePrefab(Scene& scene,
                                            const Prefab& prefab,
                                            const glm::vec3& worldOffset,
                                            const std::vector<PrefabOverride>& overrides) {
    if (prefab.entities.empty()) return kInvalidEntityID;

    std::unordered_map<EntityID, EntityID> idMap;
    EntityID newRootId = kInvalidEntityID;

    // Pass 1: Create new unique EntityIDs for every entity in the prefab
    for (const auto& [localId, src] : prefab.entities) {
        const EntityID newId = scene.createEntity(src.name);
        idMap[localId] = newId;
        if (auto* dst = scene.findEntity(newId)) {
            dst->active = src.active;
            dst->transform = src.transform;
            dst->meshAssetGuid = src.meshAssetGuid;
            dst->materialAssetGuid = src.materialAssetGuid;
            dst->prefabSourceGuid = prefab.guid;
            dst->components = src.components;
            if (localId == prefab.rootEntityId || src.parentId == kInvalidEntityID) {
                dst->transform.position += worldOffset;
                if (newRootId == kInvalidEntityID) newRootId = newId;
            }
            for (const auto& ov : overrides) {
                if (ov.targetLocalEntityId == localId) {
                    dst->prefabOverrides.push_back(ov);
                }
            }
            applyOverrides(*dst, dst->prefabOverrides);
        }
    }

    // Pass 2: Remap internal parent-child links
    for (const auto& [localId, src] : prefab.entities) {
        if (src.parentId != kInvalidEntityID) {
            auto itChild = idMap.find(localId);
            auto itParent = idMap.find(src.parentId);
            if (itChild != idMap.end() && itParent != idMap.end()) {
                scene.setParent(itChild->second, itParent->second);
            }
        }
    }

    return newRootId;
}

void SceneSerializer::applyOverrides(SceneEntity& entity, const std::vector<PrefabOverride>& overrides) {
    for (const auto& ov : overrides) {
        if (ov.componentType == "Entity" && ov.propertyKey == "active") {
            entity.active = (ov.value == "1" || ov.value == "true");
        } else if (ov.componentType == "Transform" && ov.propertyKey == "posX") {
            entity.transform.position.x = std::stof(ov.value);
        } else {
            for (auto& c : entity.components) {
                if (c.typeName == ov.componentType) {
                    c.properties[ov.propertyKey] = ov.value;
                }
            }
        }
    }
}

bool SceneSerializer::revertOverride(SceneEntity& entity, const std::string& componentType, const std::string& propertyKey) {
    auto it = std::remove_if(entity.prefabOverrides.begin(), entity.prefabOverrides.end(),
        [&](const PrefabOverride& ov) { return ov.componentType == componentType && ov.propertyKey == propertyKey; });
    if (it == entity.prefabOverrides.end()) return false;
    entity.prefabOverrides.erase(it, entity.prefabOverrides.end());
    return true;
}

void SceneSerializer::revertAllOverrides(SceneEntity& entity, const SceneEntity& sourcePrefabEntity) {
    entity.prefabOverrides.clear();
    entity.active = sourcePrefabEntity.active;
    entity.transform = sourcePrefabEntity.transform;
    entity.components = sourcePrefabEntity.components;
}

} // namespace sidera
