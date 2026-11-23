#pragma once

#include "editor/EditorTypes.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera::editor {

enum class DockSlot : uint32_t {
    Center = 0,
    Left = 1,
    Right = 2,
    Bottom = 3,
    Floating = 4
};

class IEditorPanel {
public:
    virtual ~IEditorPanel() = default;
    [[nodiscard]] virtual std::string getId() const = 0;
    [[nodiscard]] virtual std::string getDisplayName() const = 0;
    [[nodiscard]] bool isOpen() const noexcept { return m_open; }
    void setOpen(bool open) noexcept { m_open = open; }
    [[nodiscard]] DockSlot getDockSlot() const noexcept { return m_dockSlot; }
    void setDockSlot(DockSlot slot) noexcept { m_dockSlot = slot; }

    virtual void onInit() {}
    virtual void onUpdate(float /*dt*/) {}
    virtual void onRender() {}
    virtual void onShutdown() {}

protected:
    bool m_open = true;
    DockSlot m_dockSlot = DockSlot::Center;
};

// Built-in Core Panel: Console / Log with bounded ring buffer & severity filtering
class ConsoleLogPanel final : public IEditorPanel {
public:
    static constexpr size_t kMaxEntries = 1024;

    ConsoleLogPanel() { m_dockSlot = DockSlot::Bottom; }
    std::string getId() const override { return "core.console"; }
    std::string getDisplayName() const override { return "Console"; }

    void pushLog(EditorLogSeverity sev, std::string category, std::string msg) {
        if (m_entries.size() >= kMaxEntries) m_entries.erase(m_entries.begin());
        m_entries.push_back({sev, std::move(category), std::move(msg)});
    }
    void clear() noexcept { m_entries.clear(); }
    [[nodiscard]] const std::vector<EditorLogEntry>& getEntries() const noexcept { return m_entries; }

private:
    std::vector<EditorLogEntry> m_entries;
};

// Built-in Core Panel: Project Browser
class ProjectBrowserPanel final : public IEditorPanel {
public:
    ProjectBrowserPanel() { m_dockSlot = DockSlot::Left; }
    std::string getId() const override { return "core.project_browser"; }
    std::string getDisplayName() const override { return "Project Browser"; }
};

// Built-in Core Panel: Central Workspace / Future Viewport Host (M39.2 extension point)
class WorkspaceHostPanel final : public IEditorPanel {
public:
    WorkspaceHostPanel() { m_dockSlot = DockSlot::Center; }
    std::string getId() const override { return "core.workspace_host"; }
    std::string getDisplayName() const override { return "Workspace"; }
};

class EditorPanelRegistry {
public:
    static constexpr uint32_t kLayoutSchemaVersion = 1;

    bool registerPanel(std::unique_ptr<IEditorPanel> panel) {
        if (!panel || panel->getId().empty()) return false;
        const std::string id = panel->getId();
        if (m_panels.find(id) != m_panels.end()) return false; // reject duplicate ID
        panel->onInit();
        m_panels[id] = std::move(panel);
        return true;
    }

    bool unregisterPanel(const std::string& id) {
        auto it = m_panels.find(id);
        if (it == m_panels.end()) return false;
        it->second->onShutdown();
        m_panels.erase(it);
        return true;
    }

    [[nodiscard]] IEditorPanel* findPanel(const std::string& id) const noexcept {
        auto it = m_panels.find(id);
        return (it != m_panels.end()) ? it->second.get() : nullptr;
    }

    bool setPanelOpen(const std::string& id, bool open) noexcept {
        if (auto* p = findPanel(id)) { p->setOpen(open); return true; }
        return false;
    }

    void updateAll(float dt) {
        for (auto& [id, p] : m_panels) {
            if (p->isOpen()) p->onUpdate(dt);
        }
    }

    void shutdownAll() noexcept {
        for (auto& [id, p] : m_panels) p->onShutdown();
        m_panels.clear();
    }

    [[nodiscard]] size_t getPanelCount() const noexcept { return m_panels.size(); }

    bool saveLayout(const std::filesystem::path& path) const {
        std::error_code ec;
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::trunc);
        if (!f.is_open()) return false;
        f << "SIDERA_EDITOR_LAYOUT\nschema_version " << kLayoutSchemaVersion << "\n";
        for (const auto& [id, p] : m_panels) {
            f << "PANEL " << id << " " << (p->isOpen() ? 1 : 0) << " " << static_cast<uint32_t>(p->getDockSlot()) << "\n";
        }
        return f.good();
    }

    bool loadLayout(const std::filesystem::path& path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        std::string hdr;
        if (!std::getline(f, hdr) || hdr != "SIDERA_EDITOR_LAYOUT") return false;
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream iss(line);
            std::string k;
            iss >> k;
            if (k == "schema_version") {
                uint32_t ver = 0; iss >> ver;
                if (ver == 0 || ver > kLayoutSchemaVersion) return false;
            } else if (k == "PANEL") {
                std::string id; int open = 1; uint32_t slot = 0;
                iss >> id >> open >> slot;
                if (auto* p = findPanel(id)) {
                    p->setOpen(open != 0);
                    p->setDockSlot(static_cast<DockSlot>(slot <= 4 ? slot : 0));
                }
                // Unknown panel IDs in persisted layouts are safely ignored
            }
        }
        return true;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<IEditorPanel>> m_panels;
};

} // namespace sidera::editor
