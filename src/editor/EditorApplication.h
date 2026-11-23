#pragma once

#include "assets/AssetDatabase.h"
#include "editor/EditorCommand.h"
#include "editor/EditorPanel.h"
#include "editor/EditorSelection.h"
#include "editor/EditorTypes.h"
#include "scene/Scene.h"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera::editor {

// Dedicated SIDERA Editor Core Application (strictly decoupled from Engine Core & Runtime).
class EditorApplication {
public:
    EditorApplication() = default;
    ~EditorApplication() { shutdown(); }

    bool init();
    void tick(float dt);
    void shutdown() noexcept;

    // Project Browser & Project Management
    static bool createProject(const std::filesystem::path& projectDir, const std::string& projectName);
    static bool validateProjectDirectory(const std::filesystem::path& projectDir, EditorProjectInfo& outInfo);
    bool openProject(const std::filesystem::path& projectDir);
    void closeProject();

    // Recent Projects persistence
    void addRecentProject(const EditorProjectInfo& info);
    bool removeRecentProject(const std::string& directory);
    bool saveRecentProjects(const std::filesystem::path& path) const;
    bool loadRecentProjects(const std::filesystem::path& path);
    [[nodiscard]] const std::vector<EditorProjectInfo>& getRecentProjects() const noexcept { return m_recentProjects; }

    // Keyboard Shortcuts (with conflict detection & text-input focus isolation)
    bool registerShortcut(const EditorShortcut& shortcut, std::function<void()> action);
    bool dispatchKeyChord(const std::string& key, bool ctrl, bool shift, bool alt, bool textInputFocused);

    // Notifications & Dirty State
    void pushNotification(EditorLogSeverity sev, std::string title, std::string detail);
    [[nodiscard]] const std::vector<EditorNotification>& getNotifications() const noexcept { return m_notifications; }
    void setWorkspaceDirty(bool dirty) noexcept { m_workspaceDirty = dirty; }
    void setProjectDirty(bool dirty) noexcept { m_projectDirty = dirty; }
    [[nodiscard]] bool isWorkspaceDirty() const noexcept { return m_workspaceDirty; }
    [[nodiscard]] bool isProjectDirty() const noexcept { return m_projectDirty; }

    // Service Accessors (M39.2 Content Editors consume these directly)
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] EditorAppState getState() const noexcept { return m_state; }
    [[nodiscard]] const EditorProjectInfo& getActiveProject() const noexcept { return m_activeProject; }
    [[nodiscard]] EditorSettings& getSettings() noexcept { return m_settings; }
    [[nodiscard]] EditorPanelRegistry& getPanels() noexcept { return m_panels; }
    [[nodiscard]] EditorSelectionContext& getSelection() noexcept { return m_selection; }
    [[nodiscard]] EditorCommandManager& getCommands() noexcept { return m_commands; }
    [[nodiscard]] AssetDatabase& getAssetDatabase() noexcept { return m_assetDatabase; }
    [[nodiscard]] Scene& getActiveScene() noexcept { return m_activeScene; }

private:
    bool m_initialized = false;
    EditorAppState m_state = EditorAppState::NoProject;
    bool m_workspaceDirty = false;
    bool m_projectDirty = false;

    EditorSettings m_settings{};
    EditorProjectInfo m_activeProject{};
    std::vector<EditorProjectInfo> m_recentProjects;
    std::vector<EditorNotification> m_notifications;

    EditorPanelRegistry m_panels;
    EditorSelectionContext m_selection;
    EditorCommandManager m_commands;
    AssetDatabase m_assetDatabase;
    Scene m_activeScene;

    struct ShortcutBinding {
        EditorShortcut shortcut;
        std::function<void()> action;
    };
    std::vector<ShortcutBinding> m_shortcuts;
};

} // namespace sidera::editor
