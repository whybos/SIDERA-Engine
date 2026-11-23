#include "editor/EditorApplication.h"

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

class SetIntCommand final : public IEditorCommand {
public:
    SetIntCommand(int* target, int newVal, bool shouldFail = false)
        : m_target(target), m_newVal(newVal), m_fail(shouldFail) {}
    std::string getDescription() const override { return "SetInt"; }
    bool execute() override {
        if (m_fail) return false;
        m_oldVal = *m_target;
        *m_target = m_newVal;
        return true;
    }
    bool undo() override {
        *m_target = m_oldVal;
        return true;
    }
private:
    int* m_target = nullptr;
    int m_oldVal = 0;
    int m_newVal = 0;
    bool m_fail = false;
};

static void testPanelsAndLayoutPersistence() {
    const std::filesystem::path layoutPath = "editor_core_test_layout.cfg";
    EditorApplication app;
    CHECK(app.init(), "EditorApplication::init succeeds");
    CHECK(app.getPanels().getPanelCount() == 3u, "3 core panels registered (ProjectBrowser, Console, Workspace)");
    CHECK(!app.getPanels().registerPanel(std::make_unique<ConsoleLogPanel>()), "Duplicate panel ID rejected");

    CHECK(app.getPanels().setPanelOpen("core.console", false), "Console panel closed");
    CHECK(app.getPanels().saveLayout(layoutPath), "Layout saved");
    app.getPanels().setPanelOpen("core.console", true);
    CHECK(app.getPanels().loadLayout(layoutPath), "Layout restored");
    CHECK(!app.getPanels().findPanel("core.console")->isOpen(), "Persisted panel open state restored");

    std::error_code ec;
    std::filesystem::remove(layoutPath, ec);
}

static void testProjectsSelectionAndCommands() {
    const std::filesystem::path tmpDir = "editor_core_test_proj";
    const std::filesystem::path recentPath = tmpDir / "recent.cfg";
    const std::filesystem::path settingsPath = tmpDir / "settings.cfg";
    std::error_code ec;
    std::filesystem::remove_all(tmpDir, ec);

    EditorApplication app;
    app.init();

    // Project creation, validation, open, recent persistence, close
    CHECK(EditorApplication::createProject(tmpDir / "MyGame", "MyGame"), "Project created");
    CHECK(!app.openProject(tmpDir / "NonExistent"), "Missing project rejected");
    CHECK(app.openProject(tmpDir / "MyGame"), "Valid project opened");
    CHECK(app.getState() == EditorAppState::ProjectOpen && app.getActiveProject().name == "MyGame", "Active project state set");
    CHECK(app.saveRecentProjects(recentPath) && app.loadRecentProjects(recentPath) && app.getRecentProjects().size() == 1u,
          "Recent projects persisted and validated");

    // Selection & stale entity purge
    const EntityID e1 = app.getActiveScene().createEntity("Hero");
    const EntityID e2 = app.getActiveScene().createEntity("Tree");
    int notifCount = 0;
    const uint64_t tok = app.getSelection().subscribe([&]() { ++notifCount; });
    app.getSelection().select({SelectionKind::Entity, e1, {}});
    app.getSelection().addToSelection({SelectionKind::Entity, e2, {}});
    CHECK(app.getSelection().count() == 2u && notifCount == 2, "Multi-selection and notifications work");
    app.getActiveScene().removeEntity(e2);
    app.tick(1.0f / 60.0f);
    CHECK(app.getSelection().count() == 1u, "Stale deleted EntityID automatically purged from selection");
    app.getSelection().unsubscribe(tok);

    // Command system, Undo/Redo, Composite rollback, and Shortcuts
    int value = 10;
    CHECK(app.getCommands().execute(std::make_unique<SetIntCommand>(&value, 42)), "Command executed");
    CHECK(value == 42 && app.getCommands().canUndo(), "Value updated to 42");
    CHECK(!app.dispatchKeyChord("Z", true, false, false, true), "Ctrl+Z blocked while text input focused");
    CHECK(app.dispatchKeyChord("Z", true, false, false, false) && value == 10, "Ctrl+Z undid command");
    CHECK(app.dispatchKeyChord("Y", true, false, false, false) && value == 42, "Ctrl+Y redid command");

    // Composite command transactional rollback on failure
    auto comp = std::make_unique<CompositeEditorCommand>("AtomicBatch");
    comp->addSubCommand(std::make_unique<SetIntCommand>(&value, 99, false));
    comp->addSubCommand(std::make_unique<SetIntCommand>(&value, 100, true)); // fails!
    CHECK(!app.getCommands().execute(std::move(comp)), "Failing composite command returns false");
    CHECK(value == 42, "Composite command rolled back partial mutation to 42");

    // Settings save/load
    app.getSettings().uiScale = 1.25f;
    CHECK(app.getSettings().saveToFile(settingsPath), "EditorSettings saved");
    EditorSettings loadedSettings{};
    CHECK(loadedSettings.loadFromFile(settingsPath) && loadedSettings.uiScale == 1.25f, "EditorSettings loaded");

    app.closeProject();
    CHECK(app.getState() == EditorAppState::NoProject && app.getSelection().count() == 0u && !app.getCommands().canUndo(),
          "Closing project clears selection and command history");

    app.shutdown();
    std::filesystem::remove_all(tmpDir, ec);
}

int main() {
    testPanelsAndLayoutPersistence();
    testProjectsSelectionAndCommands();

    std::cout << "test_editor_core: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
