#include "editor/EditorApplication.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace sidera::editor {

bool EditorApplication::init() {
    if (m_initialized) return true;
    m_settings.validate();

    // Register core M39.1 panels
    m_panels.registerPanel(std::make_unique<ProjectBrowserPanel>());
    m_panels.registerPanel(std::make_unique<ConsoleLogPanel>());
    m_panels.registerPanel(std::make_unique<WorkspaceHostPanel>());

    // Register core global shortcuts (Ctrl+Z Undo, Ctrl+Y Redo)
    registerShortcut({"edit.undo", "Z", true, false, false, true, "Undo"}, [this]() { m_commands.undo(); });
    registerShortcut({"edit.redo", "Y", true, false, false, true, "Redo"}, [this]() { m_commands.redo(); });

    m_assetDatabase.registerDefaultImporters();
    m_state = EditorAppState::NoProject;
    m_initialized = true;
    return true;
}

void EditorApplication::tick(float dt) {
    if (!m_initialized) return;
    m_panels.updateAll(dt);
    m_selection.validate(&m_activeScene, &m_assetDatabase);
}

void EditorApplication::shutdown() noexcept {
    if (!m_initialized) return;
    m_state = EditorAppState::ShuttingDown;
    closeProject();
    m_panels.shutdownAll();
    m_shortcuts.clear();
    m_notifications.clear();
    m_initialized = false;
}

bool EditorApplication::createProject(const std::filesystem::path& projectDir, const std::string& projectName) {
    if (projectDir.empty() || projectName.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(projectDir / "Assets", ec);
    std::filesystem::create_directories(projectDir / "Scenes", ec);
    if (ec) return false;

    const auto projFile = projectDir / "project.sideraproj";
    std::ofstream f(projFile, std::ios::trunc);
    if (!f.is_open()) return false;
    f << "SIDERA_PROJECT\n"
      << "schema_version " << EditorProjectInfo::kCurrentProjectVersion << "\n"
      << "name " << projectName << "\n";
    return f.good();
}

bool EditorApplication::validateProjectDirectory(const std::filesystem::path& projectDir, EditorProjectInfo& outInfo) {
    const auto projFile = projectDir / "project.sideraproj";
    std::ifstream f(projFile);
    if (!f.is_open()) return false;
    std::string hdr;
    if (!std::getline(f, hdr) || hdr != "SIDERA_PROJECT") return false;

    EditorProjectInfo info{};
    info.directory = projectDir.generic_string();
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string k;
        iss >> k;
        if (k == "schema_version") {
            iss >> info.schemaVersion;
            if (info.schemaVersion == 0 || info.schemaVersion > EditorProjectInfo::kCurrentProjectVersion) return false;
        } else if (k == "name") {
            std::getline(iss >> std::ws, info.name);
        }
    }
    if (!info.isValid()) return false;
    outInfo = std::move(info);
    return true;
}

bool EditorApplication::openProject(const std::filesystem::path& projectDir) {
    if (!m_initialized) return false;
    m_state = EditorAppState::LoadingProject;

    EditorProjectInfo info{};
    if (!validateProjectDirectory(projectDir, info)) {
        m_state = m_activeProject.isValid() ? EditorAppState::ProjectOpen : EditorAppState::NoProject;
        pushNotification(EditorLogSeverity::Error, "Project Open Failed", "Invalid or missing project.sideraproj");
        return false;
    }

    closeProject();
    info.lastOpenedEpoch = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    m_activeProject = info;
    m_assetDatabase.setProjectRoot(projectDir);
    m_activeScene = Scene(info.name + "_Main");
    addRecentProject(info);
    m_state = EditorAppState::ProjectOpen;
    m_projectDirty = false;
    pushNotification(EditorLogSeverity::Info, "Project Opened", info.name);
    return true;
}

void EditorApplication::closeProject() {
    if (m_state == EditorAppState::NoProject && !m_activeProject.isValid()) return;
    m_state = EditorAppState::ClosingProject;
    m_selection.clear();
    m_commands.clear();
    m_activeScene.clear();
    m_activeProject = {};
    m_projectDirty = false;
    m_state = EditorAppState::NoProject;
}

void EditorApplication::addRecentProject(const EditorProjectInfo& info) {
    removeRecentProject(info.directory);
    m_recentProjects.insert(m_recentProjects.begin(), info);
    if (m_recentProjects.size() > m_settings.recentProjectLimit) {
        m_recentProjects.resize(m_settings.recentProjectLimit);
    }
}

bool EditorApplication::removeRecentProject(const std::string& directory) {
    auto it = std::remove_if(m_recentProjects.begin(), m_recentProjects.end(),
        [&](const EditorProjectInfo& p) { return p.directory == directory; });
    if (it == m_recentProjects.end()) return false;
    m_recentProjects.erase(it, m_recentProjects.end());
    return true;
}

bool EditorApplication::saveRecentProjects(const std::filesystem::path& path) const {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return false;
    f << "SIDERA_RECENT_PROJECTS\n";
    for (const auto& p : m_recentProjects) {
        f << p.directory << "|" << p.name << "|" << p.lastOpenedEpoch << "\n";
    }
    return f.good();
}

bool EditorApplication::loadRecentProjects(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string hdr;
    if (!std::getline(f, hdr) || hdr != "SIDERA_RECENT_PROJECTS") return false;
    m_recentProjects.clear();
    std::string line;
    while (std::getline(f, line)) {
        const auto p1 = line.find('|');
        const auto p2 = (p1 != std::string::npos) ? line.find('|', p1 + 1) : std::string::npos;
        if (p1 == std::string::npos || p2 == std::string::npos) continue;
        EditorProjectInfo info{};
        info.directory = line.substr(0, p1);
        info.name = line.substr(p1 + 1, p2 - p1 - 1);
        info.lastOpenedEpoch = std::stoull(line.substr(p2 + 1));
        if (std::filesystem::exists(std::filesystem::path(info.directory) / "project.sideraproj")) {
            m_recentProjects.push_back(std::move(info));
        }
    }
    return true;
}

bool EditorApplication::registerShortcut(const EditorShortcut& shortcut, std::function<void()> action) {
    for (const auto& b : m_shortcuts) {
        if (b.shortcut.id == shortcut.id ||
            b.shortcut.matchesChord(shortcut.key, shortcut.ctrl, shortcut.shift, shortcut.alt)) {
            return false; // reject duplicate ID or conflicting chord
        }
    }
    m_shortcuts.push_back({shortcut, std::move(action)});
    return true;
}

bool EditorApplication::dispatchKeyChord(const std::string& key, bool ctrl, bool shift, bool alt, bool textInputFocused) {
    if (textInputFocused) return false; // isolate global shortcuts from active text input
    for (const auto& b : m_shortcuts) {
        if (b.shortcut.matchesChord(key, ctrl, shift, alt)) {
            if (b.action) b.action();
            return true;
        }
    }
    return false;
}

void EditorApplication::pushNotification(EditorLogSeverity sev, std::string title, std::string detail) {
    if (m_notifications.size() >= 128u) m_notifications.erase(m_notifications.begin());
    m_notifications.push_back({sev, std::move(title), std::move(detail)});
    if (auto* console = dynamic_cast<ConsoleLogPanel*>(m_panels.findPanel("core.console"))) {
        console->pushLog(sev, "Editor", m_notifications.back().title + ": " + m_notifications.back().detail);
    }
}

} // namespace sidera::editor
