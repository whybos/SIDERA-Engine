#pragma once

#include "assets/AssetDatabase.h"
#include "audio/AudioSystem.h"
#include "editor/EditorCommand.h"
#include "editor/EditorPanel.h"
#include "editor/EditorSelection.h"
#include "editor/content/SceneEditCommands.h"
#include "scene/Scene.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sidera::editor {

enum class GizmoMode : uint32_t { Translate = 0, Rotate = 1, Scale = 2 };
enum class GizmoSpace : uint32_t { World = 0, Local = 1 };
enum class ViewportShadingMode : uint32_t { Lit = 0, Unlit = 1, Wireframe = 2, Normals = 3, Depth = 4 };

// 1. Scene Viewport Panel (Editor-only camera, ray picking, transform gizmos, grid & snapping)
class SceneViewportPanel final : public IEditorPanel {
public:
    SceneViewportPanel() { m_dockSlot = DockSlot::Center; }
    std::string getId() const override { return "content.scene_viewport"; }
    std::string getDisplayName() const override { return "Scene Viewport"; }

    glm::vec3 cameraPosition{0.0f, 5.0f, 12.0f};
    glm::vec3 cameraTarget{0.0f, 0.0f, 0.0f};
    float cameraSpeed = 10.0f;
    ViewportShadingMode shadingMode = ViewportShadingMode::Lit;
    GizmoMode gizmoMode = GizmoMode::Translate;
    GizmoSpace gizmoSpace = GizmoSpace::World;
    bool gridVisible = true;
    float gridSize = 1.0f;
    bool snapEnabled = false;
    float snapIncrement = 0.5f;

    void focusEntity(const Scene& scene, EntityID id) noexcept {
        if (const auto* e = scene.findEntity(id)) {
            cameraTarget = e->transform.position;
            cameraPosition = cameraTarget + glm::vec3(0.0f, 3.0f, 8.0f);
        }
    }

    void resetCamera() noexcept {
        cameraPosition = glm::vec3(0.0f, 5.0f, 12.0f);
        cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    }

    [[nodiscard]] glm::vec3 snapVector(const glm::vec3& v) const noexcept {
        if (!snapEnabled || snapIncrement <= 1e-4f) return v;
        return glm::vec3(
            std::round(v.x / snapIncrement) * snapIncrement,
            std::round(v.y / snapIncrement) * snapIncrement,
            std::round(v.z / snapIncrement) * snapIncrement);
    }

    // Ray-sphere entity picking against active Scene (world positions)
    [[nodiscard]] EntityID pickEntity(const Scene& scene, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float pickRadius = 1.0f) const noexcept {
        const glm::vec3 dir = glm::normalize(rayDir);
        EntityID bestId = kInvalidEntityID;
        float bestT = 1e30f;
        for (const auto& [id, e] : scene.getEntities()) {
            if (!e.active) continue;
            const glm::mat4 world = scene.computeWorldMatrix(id);
            const glm::vec3 center(world[3].x, world[3].y, world[3].z);
            const glm::vec3 oc = rayOrigin - center;
            const float b = glm::dot(oc, dir);
            const float c = glm::dot(oc, oc) - pickRadius * pickRadius;
            const float disc = b * b - c;
            if (disc >= 0.0f) {
                const float t = -b - std::sqrt(disc);
                if (t >= 0.0f && t < bestT) {
                    bestT = t;
                    bestId = id;
                }
            }
        }
        return bestId;
    }

    bool applyGizmoTranslate(Scene& scene, EditorCommandManager& commands, EntityID id, const glm::vec3& delta) const {
        const auto* e = scene.findEntity(id);
        if (!e) return false;
        TransformComponent next = e->transform;
        next.position = snapVector(next.position + delta);
        return commands.execute(std::make_unique<TransformEntityCommand>(&scene, id, next));
    }
};

// 2. Hierarchy Panel (Scene tree, search/filter, undoable CRUD & cycle-safe reparenting)
class HierarchyPanel final : public IEditorPanel {
public:
    HierarchyPanel() { m_dockSlot = DockSlot::Left; }
    std::string getId() const override { return "content.hierarchy"; }
    std::string getDisplayName() const override { return "Hierarchy"; }

    std::string searchFilter;

    [[nodiscard]] std::vector<EntityID> getFilteredEntities(const Scene& scene) const {
        std::vector<EntityID> out;
        for (const auto& [id, e] : scene.getEntities()) {
            if (searchFilter.empty() || e.name.find(searchFilter) != std::string::npos) {
                out.push_back(id);
            }
        }
        return out;
    }
};

// 3. Inspector Panel (Entity & Component property editing backed by EditorCommandManager)
class InspectorPanel final : public IEditorPanel {
public:
    InspectorPanel() { m_dockSlot = DockSlot::Right; }
    std::string getId() const override { return "content.inspector"; }
    std::string getDisplayName() const override { return "Inspector"; }

    bool editComponentProperty(Scene& scene, EditorCommandManager& commands,
                               EntityID id, const std::string& compType,
                               const std::string& key, const std::string& value) const {
        return commands.execute(std::make_unique<SetComponentPropertyCommand>(&scene, id, compType, key, value));
    }
};

// 4. Asset Browser Panel (Direct M35 AssetDatabase view, filtering & import)
class AssetBrowserPanel final : public IEditorPanel {
public:
    AssetBrowserPanel() { m_dockSlot = DockSlot::Bottom; }
    std::string getId() const override { return "content.asset_browser"; }
    std::string getDisplayName() const override { return "Asset Browser"; }

    AssetType filterType = AssetType::Unknown; // Unknown = All
    std::string searchQuery;
};

// 5. Material Editor Panel (PBR parameter editing with validation)
class MaterialEditorPanel final : public IEditorPanel {
public:
    MaterialEditorPanel() { m_dockSlot = DockSlot::Right; m_open = false; }
    std::string getId() const override { return "content.material_editor"; }
    std::string getDisplayName() const override { return "Material Editor"; }

    glm::vec3 baseColor{1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    void validate() noexcept {
        metallic = std::clamp(metallic, 0.0f, 1.0f);
        roughness = std::clamp(roughness, 0.04f, 1.0f);
    }
};

// 6. Lighting & Environment Panel
class LightingEnvironmentPanel final : public IEditorPanel {
public:
    LightingEnvironmentPanel() { m_dockSlot = DockSlot::Right; m_open = false; }
    std::string getId() const override { return "content.lighting_env"; }
    std::string getDisplayName() const override { return "Lighting & Environment"; }

    float sunIntensity = 2.6f;
    float exposureEV = 0.0f;
    bool enableVolumetrics = true;
    bool enableSSGI = true;
    bool enableSSR = true;
};

// 7. Audio Editor Panel (Direct M38 AudioSystem preview & bus inspection)
class AudioEditorPanel final : public IEditorPanel {
public:
    AudioEditorPanel() { m_dockSlot = DockSlot::Bottom; m_open = false; }
    std::string getId() const override { return "content.audio_editor"; }
    std::string getDisplayName() const override { return "Audio Editor"; }

    uint64_t previewClip(AudioSystem& audio, const AssetGUID& clipGuid, float volume = 1.0f) {
        m_lastPreviewSourceId = audio.playOneShot(clipGuid, glm::vec3(0.0f), volume, "UI");
        return m_lastPreviewSourceId;
    }
    void stopPreview(AudioSystem& audio) {
        if (m_lastPreviewSourceId != 0) {
            audio.stop(m_lastPreviewSourceId);
            m_lastPreviewSourceId = 0;
        }
    }

private:
    uint64_t m_lastPreviewSourceId = 0;
};

// 8. Diagnostics & Profiler Panel (M33 GPU/CPU Profiler & M34 MemoryBudgetManager view)
class ProfilerDiagnosticsPanel final : public IEditorPanel {
public:
    ProfilerDiagnosticsPanel() { m_dockSlot = DockSlot::Bottom; m_open = false; }
    std::string getId() const override { return "content.profiler"; }
    std::string getDisplayName() const override { return "Profiler & Diagnostics"; }
};

} // namespace sidera::editor
