#pragma once

#include "editor/EditorCommand.h"
#include "scene/Scene.h"

#include <cmath>
#include <string>

namespace sidera::editor {

class CreateEntityCommand final : public IEditorCommand {
public:
    CreateEntityCommand(Scene* scene, std::string name, EntityID parentId = kInvalidEntityID)
        : m_scene(scene), m_name(std::move(name)), m_parentId(parentId) {}

    std::string getDescription() const override { return "Create Entity: " + m_name; }
    bool validate() const override { return m_scene != nullptr && !m_name.empty(); }

    bool execute() override {
        if (!m_scene) return false;
        m_createdId = (m_createdId == kInvalidEntityID)
            ? m_scene->createEntity(m_name)
            : m_scene->createEntity(m_name, m_createdId);
        if (m_createdId == kInvalidEntityID) return false;
        if (m_parentId != kInvalidEntityID) m_scene->setParent(m_createdId, m_parentId);
        return true;
    }

    bool undo() override {
        return m_scene && m_createdId != kInvalidEntityID && m_scene->removeEntity(m_createdId);
    }

    [[nodiscard]] EntityID getCreatedEntityId() const noexcept { return m_createdId; }

private:
    Scene* m_scene = nullptr;
    std::string m_name;
    EntityID m_parentId = kInvalidEntityID;
    EntityID m_createdId = kInvalidEntityID;
};

class DeleteEntityCommand final : public IEditorCommand {
public:
    DeleteEntityCommand(Scene* scene, EntityID targetId) : m_scene(scene), m_targetId(targetId) {}

    std::string getDescription() const override { return "Delete Entity"; }
    bool validate() const override { return m_scene && m_scene->findEntity(m_targetId) != nullptr; }

    bool execute() override {
        if (!m_scene) return false;
        const auto* e = m_scene->findEntity(m_targetId);
        if (!e) return false;
        m_snapshot = *e;
        return m_scene->removeEntity(m_targetId);
    }

    bool undo() override {
        return m_scene && m_scene->insertEntity(m_snapshot);
    }

private:
    Scene* m_scene = nullptr;
    EntityID m_targetId = kInvalidEntityID;
    SceneEntity m_snapshot{};
};

class RenameEntityCommand final : public IEditorCommand {
public:
    RenameEntityCommand(Scene* scene, EntityID id, std::string newName)
        : m_scene(scene), m_id(id), m_newName(std::move(newName)) {}

    std::string getDescription() const override { return "Rename Entity"; }
    bool validate() const override { return m_scene && m_scene->findEntity(m_id) != nullptr && !m_newName.empty(); }

    bool execute() override {
        auto* e = m_scene ? m_scene->findEntity(m_id) : nullptr;
        if (!e) return false;
        m_oldName = e->name;
        e->name = m_newName;
        return true;
    }

    bool undo() override {
        auto* e = m_scene ? m_scene->findEntity(m_id) : nullptr;
        if (!e) return false;
        e->name = m_oldName;
        return true;
    }

private:
    Scene* m_scene = nullptr;
    EntityID m_id = kInvalidEntityID;
    std::string m_oldName;
    std::string m_newName;
};

class ReparentEntityCommand final : public IEditorCommand {
public:
    ReparentEntityCommand(Scene* scene, EntityID childId, EntityID newParentId)
        : m_scene(scene), m_childId(childId), m_newParentId(newParentId) {}

    std::string getDescription() const override { return "Reparent Entity"; }
    bool validate() const override {
        return m_scene && m_scene->findEntity(m_childId) != nullptr &&
               !m_scene->wouldCreateCycle(m_childId, m_newParentId);
    }

    bool execute() override {
        auto* e = m_scene ? m_scene->findEntity(m_childId) : nullptr;
        if (!e) return false;
        m_oldParentId = e->parentId;
        return m_scene->setParent(m_childId, m_newParentId);
    }

    bool undo() override {
        return m_scene && m_scene->setParent(m_childId, m_oldParentId);
    }

private:
    Scene* m_scene = nullptr;
    EntityID m_childId = kInvalidEntityID;
    EntityID m_oldParentId = kInvalidEntityID;
    EntityID m_newParentId = kInvalidEntityID;
};

class TransformEntityCommand final : public IEditorCommand {
public:
    TransformEntityCommand(Scene* scene, EntityID id, const TransformComponent& newTransform)
        : m_scene(scene), m_id(id), m_newTransform(newTransform) {}

    std::string getDescription() const override { return "Transform Entity"; }
    bool validate() const override { return m_scene && m_scene->findEntity(m_id) != nullptr; }

    bool execute() override {
        auto* e = m_scene ? m_scene->findEntity(m_id) : nullptr;
        if (!e) return false;
        m_oldTransform = e->transform;
        e->transform = m_newTransform;
        return true;
    }

    bool undo() override {
        auto* e = m_scene ? m_scene->findEntity(m_id) : nullptr;
        if (!e) return false;
        e->transform = m_oldTransform;
        return true;
    }

private:
    Scene* m_scene = nullptr;
    EntityID m_id = kInvalidEntityID;
    TransformComponent m_oldTransform{};
    TransformComponent m_newTransform{};
};

class SetComponentPropertyCommand final : public IEditorCommand {
public:
    SetComponentPropertyCommand(Scene* scene, EntityID id, std::string compType, std::string key, std::string value)
        : m_scene(scene), m_id(id), m_compType(std::move(compType)), m_key(std::move(key)), m_newValue(std::move(value)) {}

    std::string getDescription() const override { return "Edit " + m_compType + "." + m_key; }
    bool validate() const override { return m_scene && m_scene->findEntity(m_id) != nullptr && !m_compType.empty() && !m_key.empty(); }

    bool execute() override {
        auto* e = m_scene ? m_scene->findEntity(m_id) : nullptr;
        if (!e) return false;
        for (auto& c : e->components) {
            if (c.typeName == m_compType) {
                auto it = c.properties.find(m_key);
                m_hadPrev = (it != c.properties.end());
                if (m_hadPrev) m_oldValue = it->second;
                c.properties[m_key] = m_newValue;
                return true;
            }
        }
        return false;
    }

    bool undo() override {
        auto* e = m_scene ? m_scene->findEntity(m_id) : nullptr;
        if (!e) return false;
        for (auto& c : e->components) {
            if (c.typeName == m_compType) {
                if (m_hadPrev) c.properties[m_key] = m_oldValue;
                else c.properties.erase(m_key);
                return true;
            }
        }
        return false;
    }

private:
    Scene* m_scene = nullptr;
    EntityID m_id = kInvalidEntityID;
    std::string m_compType;
    std::string m_key;
    std::string m_newValue;
    std::string m_oldValue;
    bool m_hadPrev = false;
};

} // namespace sidera::editor
