#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera::runtime {

struct SaveGameData {
    static constexpr uint32_t kCurrentSaveVersion = 1;

    uint32_t schemaVersion = kCurrentSaveVersion;
    std::string slotId;
    std::string sceneName;
    std::map<std::string, std::string> variables; // deterministic persistent world/player state
};

// Transactional Runtime Save/Load System (atomic .tmp + rename, version check, corruption safe).
class SaveSystem {
public:
    explicit SaveSystem(std::filesystem::path saveRoot) : m_saveRoot(std::move(saveRoot)) {}

    bool saveGame(const SaveGameData& data) const {
        if (data.slotId.empty()) return false;
        std::error_code ec;
        std::filesystem::create_directories(m_saveRoot, ec);
        const auto dst = m_saveRoot / (data.slotId + ".siderasave");
        const auto tmp = dst.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f.is_open()) return false;
            f << "SIDERA_SAVE\n"
              << "schema_version " << data.schemaVersion << "\n"
              << "slot " << data.slotId << "\n"
              << "scene " << data.sceneName << "\n";
            for (const auto& [k, v] : data.variables) {
                f << "var " << k << " " << v << "\n";
            }
            f.flush();
            if (!f.good()) return false;
        }
        std::filesystem::rename(tmp, dst, ec);
        if (ec) {
            std::filesystem::remove(dst, ec);
            std::filesystem::rename(tmp, dst, ec);
        }
        return !ec;
    }

    bool loadGame(const std::string& slotId, SaveGameData& outData) const {
        const auto src = m_saveRoot / (slotId + ".siderasave");
        std::ifstream f(src);
        if (!f.is_open()) return false;
        std::string hdr;
        if (!std::getline(f, hdr) || hdr != "SIDERA_SAVE") return false;

        SaveGameData tmp{};
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream iss(line);
            std::string k;
            iss >> k;
            if (k == "schema_version") {
                iss >> tmp.schemaVersion;
                if (tmp.schemaVersion == 0 || tmp.schemaVersion > SaveGameData::kCurrentSaveVersion) return false;
            } else if (k == "slot") {
                iss >> tmp.slotId;
            } else if (k == "scene") {
                std::getline(iss >> std::ws, tmp.sceneName);
            } else if (k == "var") {
                std::string vk, vv;
                iss >> vk;
                std::getline(iss >> std::ws, vv);
                tmp.variables[vk] = vv;
            }
        }
        if (tmp.slotId.empty()) return false;
        outData = std::move(tmp); // transactional commit
        return true;
    }

    bool deleteSave(const std::string& slotId) const {
        std::error_code ec;
        return std::filesystem::remove(m_saveRoot / (slotId + ".siderasave"), ec);
    }

    [[nodiscard]] std::vector<std::string> listSaveSlots() const {
        std::vector<std::string> slots;
        std::error_code ec;
        if (!std::filesystem::exists(m_saveRoot, ec)) return slots;
        for (const auto& entry : std::filesystem::directory_iterator(m_saveRoot, ec)) {
            if (entry.path().extension() == ".siderasave") {
                slots.push_back(entry.path().stem().string());
            }
        }
        return slots;
    }

private:
    std::filesystem::path m_saveRoot;
};

// Localization Hooks (locale switching, fallback locale, missing-key handling, {0} substitution).
class LocalizationManager {
public:
    void setActiveLocale(std::string locale) { m_activeLocale = std::move(locale); }
    void setFallbackLocale(std::string locale) { m_fallbackLocale = std::move(locale); }

    void registerString(const std::string& locale, const std::string& key, const std::string& value) {
        m_tables[locale][key] = value;
    }

    [[nodiscard]] std::string lookup(const std::string& key) const {
        auto tryLocale = [&](const std::string& loc) -> const std::string* {
            auto lit = m_tables.find(loc);
            if (lit == m_tables.end()) return nullptr;
            auto kit = lit->second.find(key);
            return (kit != lit->second.end()) ? &kit->second : nullptr;
        };
        if (const auto* v = tryLocale(m_activeLocale)) return *v;
        if (const auto* v = tryLocale(m_fallbackLocale)) return *v;
        return "[" + key + "]"; // explicit missing-key marker
    }

    [[nodiscard]] std::string format(const std::string& key, const std::string& arg0) const {
        std::string text = lookup(key);
        const auto pos = text.find("{0}");
        if (pos != std::string::npos) text.replace(pos, 3, arg0);
        return text;
    }

private:
    std::string m_activeLocale = "en-US";
    std::string m_fallbackLocale = "en-US";
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_tables;
};

} // namespace sidera::runtime
