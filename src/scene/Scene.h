#pragma once

#include "assets/AssetDatabase.h"
#include "scene/SceneTypes.h"

#include <glm/mat4x4.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sidera {

// Editor-ready Scene data model with stable EntityIDs, validated parent-child hierarchy,
// deterministic ordering, and AssetDatabase GUID integration (Milestone 36).
class Scene {
public:
    static constexpr uint32_t kCurrentSchemaVersion = 1;

    Scene() : m_guid(AssetGUID::generate()) {}
    explicit Scene(std::string name) : m_guid(AssetGUID::generate()), m_name(std::move(name)) {}

    [[nodiscard]] const AssetGUID& getGUID() const noexcept { return m_guid; }
    void setGUID(const AssetGUID& g) noexcept { m_guid = g; }

    [[nodiscard]] const std::string& getName() const noexcept { return m_name; }
    void setName(std::string n) { m_name = std::move(n); }

    [[nodiscard]] uint32_t getSchemaVersion() const noexcept { return m_schemaVersion; }
    void setSchemaVersion(uint32_t v) noexcept { m_schemaVersion = v; }

    // Entity management (stable IDs, never array indices)
    EntityID createEntity(const std::string& name, EntityID explicitId = kInvalidEntityID);
    bool insertEntity(const SceneEntity& entity);
    bool removeEntity(EntityID id);
    [[nodiscard]] SceneEntity* findEntity(EntityID id) noexcept;
    [[nodiscard]] const SceneEntity* findEntity(EntityID id) const noexcept;
    [[nodiscard]] const std::map<EntityID, SceneEntity>& getEntities() const noexcept { return m_entities; }
    [[nodiscard]] size_t getEntityCount() const noexcept { return m_entities.size(); }
    void clear() noexcept { m_entities.clear(); m_nextEntityId = 1; }

    // Hierarchy & transforms
    bool setParent(EntityID childId, EntityID parentId);
    [[nodiscard]] bool wouldCreateCycle(EntityID childId, EntityID newParentId) const noexcept;
    [[nodiscard]] glm::mat4 computeWorldMatrix(EntityID id) const noexcept;

    // Validation & AssetDatabase dependency gathering
    [[nodiscard]] std::vector<AssetGUID> collectAssetDependencies() const;
    [[nodiscard]] std::vector<SceneValidationIssue> validate(const AssetDatabase* assetDb = nullptr) const;

private:
    AssetGUID m_guid{};
    std::string m_name = "UntitledScene";
    uint32_t m_schemaVersion = kCurrentSchemaVersion;
    EntityID m_nextEntityId = 1;
    std::map<EntityID, SceneEntity> m_entities; // ordered by EntityID for deterministic serialization
};

// Reusable Prefab asset (entity hierarchy template with stable Prefab GUID)
struct Prefab {
    static constexpr uint32_t kCurrentSchemaVersion = 1;

    AssetGUID guid{};
    std::string name = "UntitledPrefab";
    uint32_t schemaVersion = kCurrentSchemaVersion;
    EntityID rootEntityId = kInvalidEntityID;
    std::map<EntityID, SceneEntity> entities;
};

} // namespace sidera
