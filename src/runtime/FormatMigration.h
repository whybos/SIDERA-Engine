#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera::runtime {

enum class FormatCompatibility : uint8_t {
    FullyCompatible = 0,
    RequiresMigration,
    RequiresReimport,
    UnsupportedFutureVersion,
    InvalidFormat
};

struct FormatSchemaInfo {
    std::string formatId;      // e.g. "sideraproj", "siderascene", "siderasave"
    uint32_t minSupported = 1;
    uint32_t currentVersion = 1;
};

struct VersionedDocument {
    std::string formatId;
    uint32_t schemaVersion = 1;
    std::map<std::string, std::string> fields;        // known fields
    std::map<std::string, std::string> unknownFields; // preserved non-destructively
};

using MigrationStepFn = std::function<bool(VersionedDocument&, std::string& outError)>;

// Centralized schema compatibility & non-destructive migration registry for SIDERA 1.0.
class FormatMigrationRegistry {
public:
    void registerFormat(FormatSchemaInfo info) {
        const std::string id = info.formatId;
        m_schemas[id] = std::move(info);
    }

    // Registers a step migrating fromVersion -> fromVersion + 1.
    void registerMigration(const std::string& formatId, uint32_t fromVersion, MigrationStepFn fn) {
        m_steps[formatId][fromVersion] = std::move(fn);
    }

    [[nodiscard]] FormatCompatibility checkCompatibility(const std::string& formatId,
                                                         uint32_t docVersion) const {
        auto it = m_schemas.find(formatId);
        if (it == m_schemas.end() || docVersion == 0) return FormatCompatibility::InvalidFormat;
        if (docVersion > it->second.currentVersion) return FormatCompatibility::UnsupportedFutureVersion;
        if (docVersion < it->second.minSupported) return FormatCompatibility::RequiresReimport;
        if (docVersion < it->second.currentVersion) return FormatCompatibility::RequiresMigration;
        return FormatCompatibility::FullyCompatible;
    }

    // Non-destructively migrates a document to the current schema version while preserving unknownFields.
    bool migrateToCurrent(VersionedDocument& doc, std::string& outError) const {
        const auto compat = checkCompatibility(doc.formatId, doc.schemaVersion);
        if (compat == FormatCompatibility::FullyCompatible) return true;
        if (compat == FormatCompatibility::UnsupportedFutureVersion) {
            outError = "Unsupported future schema version " + std::to_string(doc.schemaVersion) +
                       " for format '" + doc.formatId + "'";
            return false;
        }
        if (compat != FormatCompatibility::RequiresMigration) {
            outError = "Format '" + doc.formatId + "' version " + std::to_string(doc.schemaVersion) +
                       " cannot be migrated automatically";
            return false;
        }

        const auto& schema = m_schemas.at(doc.formatId);
        VersionedDocument working = doc; // transactional copy (non-destructive on failure)
        auto fit = m_steps.find(doc.formatId);
        while (working.schemaVersion < schema.currentVersion) {
            if (fit == m_steps.end() || fit->second.count(working.schemaVersion) == 0) {
                outError = "Missing migration step for '" + doc.formatId + "' from v" +
                           std::to_string(working.schemaVersion);
                return false;
            }
            if (!fit->second.at(working.schemaVersion)(working, outError)) {
                return false;
            }
            ++working.schemaVersion;
        }
        doc = std::move(working);
        return true;
    }

    static FormatMigrationRegistry createDefaultSidera10Registry() {
        FormatMigrationRegistry reg;
        reg.registerFormat({"sideraproj",   1, 1});
        reg.registerFormat({"siderameta",   1, 1});
        reg.registerFormat({"siderascene",  1, 1});
        reg.registerFormat({"sideraprefab", 1, 1});
        reg.registerFormat({"siderasave",   1, 1});
        reg.registerFormat({"sideraeditor", 1, 1});
        reg.registerFormat({"sideralayout", 1, 1});
        reg.registerFormat({"frameworkdef", 1, 1});
        return reg;
    }

private:
    std::unordered_map<std::string, FormatSchemaInfo> m_schemas;
    std::unordered_map<std::string, std::map<uint32_t, MigrationStepFn>> m_steps;
};

} // namespace sidera::runtime
