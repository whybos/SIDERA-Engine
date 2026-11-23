#include "scene/Scene.h"

#include <algorithm>
#include <unordered_set>

namespace sidera {

EntityID Scene::createEntity(const std::string& name, EntityID explicitId) {
    EntityID id = (explicitId != kInvalidEntityID) ? explicitId : m_nextEntityId++;
    if (m_entities.find(id) != m_entities.end()) {
        return kInvalidEntityID; // reject duplicate ID
    }
    m_nextEntityId = std::max(m_nextEntityId, id + 1u);
    SceneEntity e{};
    e.id = id;
    e.name = name;
    m_entities[id] = std::move(e);
    return id;
}

bool Scene::insertEntity(const SceneEntity& entity) {
    if (entity.id == kInvalidEntityID || m_entities.find(entity.id) != m_entities.end()) {
        return false;
    }
    m_nextEntityId = std::max(m_nextEntityId, entity.id + 1u);
    m_entities[entity.id] = entity;
    return true;
}

bool Scene::removeEntity(EntityID id) {
    auto it = m_entities.find(id);
    if (it == m_entities.end()) return false;
    // Detach any children referencing this entity
    for (auto& [cid, child] : m_entities) {
        if (child.parentId == id) child.parentId = kInvalidEntityID;
    }
    m_entities.erase(it);
    return true;
}

SceneEntity* Scene::findEntity(EntityID id) noexcept {
    auto it = m_entities.find(id);
    return (it != m_entities.end()) ? &it->second : nullptr;
}

const SceneEntity* Scene::findEntity(EntityID id) const noexcept {
    auto it = m_entities.find(id);
    return (it != m_entities.end()) ? &it->second : nullptr;
}

bool Scene::wouldCreateCycle(EntityID childId, EntityID newParentId) const noexcept {
    if (newParentId == kInvalidEntityID) return false;
    if (childId == newParentId) return true;
    EntityID curr = newParentId;
    uint32_t guard = 0;
    while (curr != kInvalidEntityID && guard++ < 4096u) {
        if (curr == childId) return true;
        const auto* e = findEntity(curr);
        if (!e) break;
        curr = e->parentId;
    }
    return false;
}

bool Scene::setParent(EntityID childId, EntityID parentId) {
    auto* child = findEntity(childId);
    if (!child) return false;
    if (parentId != kInvalidEntityID && !findEntity(parentId)) return false;
    if (wouldCreateCycle(childId, parentId)) return false;
    child->parentId = parentId;
    return true;
}

glm::mat4 Scene::computeWorldMatrix(EntityID id) const noexcept {
    const auto* e = findEntity(id);
    if (!e) return glm::mat4(1.0f);
    glm::mat4 world = e->transform.computeLocalMatrix();
    EntityID curr = e->parentId;
    uint32_t guard = 0;
    while (curr != kInvalidEntityID && guard++ < 4096u) {
        const auto* p = findEntity(curr);
        if (!p) break;
        world = p->transform.computeLocalMatrix() * world;
        curr = p->parentId;
    }
    return world;
}

std::vector<AssetGUID> Scene::collectAssetDependencies() const {
    std::unordered_set<AssetGUID, AssetGUIDHash> unique;
    for (const auto& [id, e] : m_entities) {
        if (e.meshAssetGuid.isValid()) unique.insert(e.meshAssetGuid);
        if (e.materialAssetGuid.isValid()) unique.insert(e.materialAssetGuid);
        if (e.prefabSourceGuid.isValid()) unique.insert(e.prefabSourceGuid);
    }
    return std::vector<AssetGUID>(unique.begin(), unique.end());
}

std::vector<SceneValidationIssue> Scene::validate(const AssetDatabase* assetDb) const {
    std::vector<SceneValidationIssue> issues;
    for (const auto& [id, e] : m_entities) {
        if (id == kInvalidEntityID || e.id != id) {
            issues.push_back({id, true, "Invalid or mismatched EntityID."});
        }
        if (e.parentId != kInvalidEntityID) {
            if (e.parentId == id) {
                issues.push_back({id, true, "Entity cannot parent itself."});
            } else if (!findEntity(e.parentId)) {
                issues.push_back({id, true, "Missing parent EntityID."});
            } else if (wouldCreateCycle(id, e.parentId)) {
                issues.push_back({id, true, "Hierarchy cycle detected."});
            }
        }
        if (assetDb) {
            if (e.meshAssetGuid.isValid() && !assetDb->findByGUID(e.meshAssetGuid)) {
                issues.push_back({id, true, "Missing Mesh AssetGUID: " + e.meshAssetGuid.toString()});
            }
            if (e.materialAssetGuid.isValid() && !assetDb->findByGUID(e.materialAssetGuid)) {
                issues.push_back({id, true, "Missing Material AssetGUID: " + e.materialAssetGuid.toString()});
            }
        }
    }
    return issues;
}

} // namespace sidera
