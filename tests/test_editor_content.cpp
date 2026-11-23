#include "editor/EditorApplication.h"
#include "editor/content/ContentEditorPanels.h"
#include "editor/content/SceneEditCommands.h"
#include "scene/SceneSerializer.h"

#include <cmath>
#include <filesystem>
#include <iostream>

using namespace sidera;
using namespace sidera::editor;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { ++g_passed; } \
        else { ++g_failed; std::cerr << "[FAIL] " << msg << "\n"; } \
    } while (0)

static void testContentEditorsAndCommands() {
    EditorApplication app;
    CHECK(app.init(), "EditorApplication initialized");

    // Register all 8 M39.2 Content Editor panels into existing EditorPanelRegistry
    CHECK(app.getPanels().registerPanel(std::make_unique<SceneViewportPanel>()), "SceneViewportPanel registered");
    CHECK(app.getPanels().registerPanel(std::make_unique<HierarchyPanel>()), "HierarchyPanel registered");
    CHECK(app.getPanels().registerPanel(std::make_unique<InspectorPanel>()), "InspectorPanel registered");
    CHECK(app.getPanels().registerPanel(std::make_unique<AssetBrowserPanel>()), "AssetBrowserPanel registered");
    CHECK(app.getPanels().registerPanel(std::make_unique<MaterialEditorPanel>()), "MaterialEditorPanel registered");
    CHECK(app.getPanels().registerPanel(std::make_unique<LightingEnvironmentPanel>()), "LightingEnvironmentPanel registered");
    CHECK(app.getPanels().registerPanel(std::make_unique<AudioEditorPanel>()), "AudioEditorPanel registered");
    CHECK(app.getPanels().registerPanel(std::make_unique<ProfilerDiagnosticsPanel>()), "ProfilerDiagnosticsPanel registered");
    CHECK(app.getPanels().getPanelCount() == 11u, "3 core + 8 content panels == 11 registered panels");

    auto* viewport = dynamic_cast<SceneViewportPanel*>(app.getPanels().findPanel("content.scene_viewport"));
    auto* hierarchy = dynamic_cast<HierarchyPanel*>(app.getPanels().findPanel("content.hierarchy"));
    auto* inspector = dynamic_cast<InspectorPanel*>(app.getPanels().findPanel("content.inspector"));
    auto* audioPanel = dynamic_cast<AudioEditorPanel*>(app.getPanels().findPanel("content.audio_editor"));

    // 1. Hierarchy undoable entity creation, rename, reparent, delete
    Scene& scene = app.getActiveScene();
    auto createRoot = std::make_unique<CreateEntityCommand>(&scene, "TempleRoot");
    auto* rawCreateRoot = createRoot.get();
    CHECK(app.getCommands().execute(std::move(createRoot)), "CreateEntityCommand executed");
    const EntityID rootId = rawCreateRoot->getCreatedEntityId();

    auto createChild = std::make_unique<CreateEntityCommand>(&scene, "Statue", rootId);
    auto* rawCreateChild = createChild.get();
    CHECK(app.getCommands().execute(std::move(createChild)), "Child entity created");
    const EntityID childId = rawCreateChild->getCreatedEntityId();

    // Cycle reparenting rejected by command validation
    CHECK(!app.getCommands().execute(std::make_unique<ReparentEntityCommand>(&scene, rootId, childId)),
          "Cyclic reparent command rejected");

    // Rename + Undo + Redo
    CHECK(app.getCommands().execute(std::make_unique<RenameEntityCommand>(&scene, childId, "GoldenStatue")), "Rename executed");
    CHECK(scene.findEntity(childId)->name == "GoldenStatue", "Entity renamed");
    CHECK(app.getCommands().undo() && scene.findEntity(childId)->name == "Statue", "Rename undone");
    CHECK(app.getCommands().redo() && scene.findEntity(childId)->name == "GoldenStatue", "Rename redone");

    hierarchy->searchFilter = "Golden";
    CHECK(hierarchy->getFilteredEntities(scene).size() == 1u, "Hierarchy search filter finds GoldenStatue");

    // 2. Scene Viewport Gizmo Translate with Snapping + Picking + Undo
    viewport->snapEnabled = true;
    viewport->snapIncrement = 0.5f;
    CHECK(viewport->applyGizmoTranslate(scene, app.getCommands(), rootId, glm::vec3(2.3f, 0.0f, -4.1f)),
          "Gizmo translate with snapping executed");
    CHECK(std::abs(scene.findEntity(rootId)->transform.position.x - 2.5f) < 1e-4f &&
          std::abs(scene.findEntity(rootId)->transform.position.z - (-4.0f)) < 1e-4f,
          "Gizmo snapped position to 0.5 grid");

    const EntityID picked = viewport->pickEntity(scene, glm::vec3(2.5f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f), 1.0f);
    CHECK(picked == rootId, "SceneViewportPanel ray picking hits TempleRoot");

    // 3. Inspector property editing + Undo/Redo + Scene persistence
    scene.findEntity(childId)->components.push_back({"PointLight", 1, {{"intensity", "4.0"}}});
    CHECK(inspector->editComponentProperty(scene, app.getCommands(), childId, "PointLight", "intensity", "12.5"),
          "Inspector property edit executed");
    CHECK(scene.findEntity(childId)->components[0].properties["intensity"] == "12.5", "Property updated");
    CHECK(app.getCommands().undo() && scene.findEntity(childId)->components[0].properties["intensity"] == "4.0",
          "Inspector property edit undone");
    CHECK(app.getCommands().redo() && scene.findEntity(childId)->components[0].properties["intensity"] == "12.5",
          "Inspector property edit redone");

    // 4. Audio Editor preview using real M38 AudioSystem
    AudioSystem audio;
    audio.init();
    AudioClip clip{};
    clip.guid = AssetGUID::generate();
    clip.sampleRate = 48000;
    clip.channels = 1;
    clip.samples.assign(256, 0.4f);
    audio.registerClip(clip);
    const uint64_t previewId = audioPanel->previewClip(audio, clip.guid, 0.8f);
    CHECK(previewId != 0u && audio.findSource(previewId)->state == AudioPlaybackState::Playing, "AudioEditorPanel preview plays via M38 AudioSystem");
    audioPanel->stopPreview(audio);
    audio.shutdown();

    // 5. Scene save/reload round-trip persistence
    const std::filesystem::path outPath = "editor_content_test_scene.sscene";
    CHECK(SceneSerializer::saveScene(scene, outPath).success, "Edited scene saved");
    Scene reloaded;
    CHECK(SceneSerializer::loadScene(outPath, reloaded).success && reloaded.getEntityCount() == 2u,
          "Edited scene reloaded with all entities intact");
    std::error_code ec;
    std::filesystem::remove(outPath, ec);

    app.shutdown();
}

int main() {
    testContentEditorsAndCommands();

    std::cout << "test_editor_content: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
