#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace sidera::editor {

enum class EditorAppState : uint32_t {
    NoProject = 0,
    LoadingProject = 1,
    ProjectOpen = 2,
    ClosingProject = 3,
    ShuttingDown = 4
};

enum class EditorTheme : uint32_t {
    Dark = 0,
    Light = 1
};

enum class EditorLogSeverity : uint32_t {
    Info = 0,
    Warning = 1,
    Error = 2
};

struct EditorLogEntry {
    EditorLogSeverity severity = EditorLogSeverity::Info;
    std::string category;
    std::string message;
};

struct EditorNotification {
    EditorLogSeverity severity = EditorLogSeverity::Info;
    std::string title;
    std::string detail;
};

struct EditorProjectInfo {
    static constexpr uint32_t kCurrentProjectVersion = 1;

    std::string name;
    std::string directory;
    uint32_t schemaVersion = kCurrentProjectVersion;
    uint64_t lastOpenedEpoch = 0;

    [[nodiscard]] bool isValid() const noexcept { return !name.empty() && !directory.empty(); }
};

struct EditorSettings {
    static constexpr uint32_t kCurrentSchemaVersion = 1;

    uint32_t schemaVersion = kCurrentSchemaVersion;
    EditorTheme theme = EditorTheme::Dark;
    float uiScale = 1.0f;
    float fontScale = 1.0f;
    uint32_t recentProjectLimit = 10u;
    bool autosaveEnabled = true;
    uint32_t autosaveIntervalSec = 300u;

    void validate() noexcept {
        uiScale = std::clamp(uiScale, 0.75f, 2.5f);
        fontScale = std::clamp(fontScale, 0.75f, 2.5f);
        recentProjectLimit = std::clamp(recentProjectLimit, 1u, 50u);
        autosaveIntervalSec = std::clamp(autosaveIntervalSec, 30u, 3600u);
    }

    bool saveToFile(const std::filesystem::path& path) const {
        std::error_code ec;
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
        const std::filesystem::path tmp = path.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::trunc);
            if (!f.is_open()) return false;
            f << "SIDERA_EDITOR_SETTINGS\n"
              << "schema_version " << schemaVersion << "\n"
              << "theme " << static_cast<uint32_t>(theme) << "\n"
              << "ui_scale " << uiScale << "\n"
              << "font_scale " << fontScale << "\n"
              << "recent_limit " << recentProjectLimit << "\n"
              << "autosave " << (autosaveEnabled ? 1 : 0) << "\n"
              << "autosave_sec " << autosaveIntervalSec << "\n";
            f.flush();
            if (!f.good()) return false;
        }
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(path, ec);
            std::filesystem::rename(tmp, path, ec);
        }
        return !ec;
    }

    bool loadFromFile(const std::filesystem::path& path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        std::string hdr;
        if (!std::getline(f, hdr) || hdr != "SIDERA_EDITOR_SETTINGS") return false;
        EditorSettings tmp{};
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream iss(line);
            std::string k;
            iss >> k;
            if (k == "schema_version") {
                iss >> tmp.schemaVersion;
                if (tmp.schemaVersion == 0 || tmp.schemaVersion > kCurrentSchemaVersion) return false;
            } else if (k == "theme") { uint32_t v = 0; iss >> v; tmp.theme = static_cast<EditorTheme>(v <= 1 ? v : 0); }
            else if (k == "ui_scale") iss >> tmp.uiScale;
            else if (k == "font_scale") iss >> tmp.fontScale;
            else if (k == "recent_limit") iss >> tmp.recentProjectLimit;
            else if (k == "autosave") { int a = 1; iss >> a; tmp.autosaveEnabled = (a != 0); }
            else if (k == "autosave_sec") iss >> tmp.autosaveIntervalSec;
        }
        tmp.validate();
        *this = tmp;
        return true;
    }
};

struct EditorShortcut {
    std::string id;
    std::string key; // e.g. "Z", "Y", "S", "O"
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool global = true;
    std::string description;

    [[nodiscard]] bool matchesChord(const std::string& k, bool c, bool s, bool a) const noexcept {
        return key == k && ctrl == c && shift == s && alt == a;
    }
};

} // namespace sidera::editor
