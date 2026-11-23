#pragma once

#include "assets/AssetImporter.h"
#include "assets/AssetTypes.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sidera {

// Centralized, Editor-ready Asset Database, Dependency Graph, Incremental Importer & Cooker (Milestone 35).
class AssetDatabase {
public:
    AssetDatabase() = default;
    ~AssetDatabase() = default;

    void setProjectRoot(const std::filesystem::path& root) { m_projectRoot = root; }
    [[nodiscard]] const std::filesystem::path& getProjectRoot() const noexcept { return m_projectRoot; }

    void registerImporter(std::unique_ptr<IAssetImporter> importer);
    void registerDefaultImporters();
    [[nodiscard]] IAssetImporter* findImporterForExtension(const std::string& ext) const noexcept;

    // Asset registration, lookup, relocation & removal
    AssetGUID registerAsset(const AssetMetadata& meta);
    bool removeAsset(const AssetGUID& guid);
    bool relocateAsset(const AssetGUID& guid, const std::string& newRelativePath);
    [[nodiscard]] const AssetMetadata* findByGUID(const AssetGUID& guid) const noexcept;
    [[nodiscard]] const AssetMetadata* findByPath(const std::string& relativePath) const noexcept;
    [[nodiscard]] size_t getAssetCount() const noexcept { return m_assets.size(); }

    // Dependency graph
    bool addDependency(const AssetGUID& asset, const AssetGUID& dependency);
    [[nodiscard]] std::vector<AssetGUID> getDependencies(const AssetGUID& asset) const;
    [[nodiscard]] std::vector<AssetGUID> getReverseDependents(const AssetGUID& asset) const;
    [[nodiscard]] bool hasCircularDependency(const AssetGUID& start) const;

    // Incremental import & reimport detection
    [[nodiscard]] bool needsReimport(const AssetGUID& guid) const;
    AssetGUID importFile(const std::string& relativeSourcePath,
                         const std::filesystem::path& cookedOutputDir,
                         const AssetImportSettings& settings = {});

    // Validation & cooking
    [[nodiscard]] std::vector<AssetValidationIssue> validateAll() const;
    AssetCookReport cookAll(const std::filesystem::path& cookedOutputDir);

    // Metadata persistence (JSON)
    bool saveManifestJson(const std::filesystem::path& outPath) const;

private:
    std::filesystem::path m_projectRoot;
    std::vector<std::unique_ptr<IAssetImporter>> m_importers;
    std::unordered_map<AssetGUID, AssetMetadata, AssetGUIDHash> m_assets;
    std::unordered_map<std::string, AssetGUID> m_pathIndex;
    std::unordered_map<AssetGUID, std::unordered_set<AssetGUID, AssetGUIDHash>, AssetGUIDHash> m_reverseDeps;
};

} // namespace sidera
