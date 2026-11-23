#include "assets/AssetDatabase.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <queue>

namespace sidera {

namespace {

std::string toLowerExt(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace

void AssetDatabase::registerImporter(std::unique_ptr<IAssetImporter> importer) {
    if (importer) m_importers.push_back(std::move(importer));
}

void AssetDatabase::registerDefaultImporters() {
    registerImporter(std::make_unique<OBJMeshImporter>());
    registerImporter(std::make_unique<TextureMetadataImporter>());
    registerImporter(std::make_unique<WAVAudioImporter>());
    registerImporter(std::make_unique<ShaderAssetImporter>());
}

IAssetImporter* AssetDatabase::findImporterForExtension(const std::string& ext) const noexcept {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& imp : m_importers) {
        if (imp->supportsExtension(lower)) return imp.get();
    }
    return nullptr;
}

AssetGUID AssetDatabase::registerAsset(const AssetMetadata& meta) {
    AssetMetadata m = meta;
    if (!m.guid.isValid()) m.guid = AssetGUID::generate();
    m_assets[m.guid] = m;
    if (!m.relativeSourcePath.empty()) {
        m_pathIndex[m.relativeSourcePath] = m.guid;
    }
    for (const auto& dep : m.dependencies) {
        m_reverseDeps[dep].insert(m.guid);
    }
    return m.guid;
}

bool AssetDatabase::removeAsset(const AssetGUID& guid) {
    auto it = m_assets.find(guid);
    if (it == m_assets.end()) return false;
    m_pathIndex.erase(it->second.relativeSourcePath);
    for (const auto& dep : it->second.dependencies) {
        m_reverseDeps[dep].erase(guid);
    }
    m_reverseDeps.erase(guid);
    m_assets.erase(it);
    return true;
}

bool AssetDatabase::relocateAsset(const AssetGUID& guid, const std::string& newRelativePath) {
    auto it = m_assets.find(guid);
    if (it == m_assets.end() || newRelativePath.empty()) return false;
    m_pathIndex.erase(it->second.relativeSourcePath);
    it->second.relativeSourcePath = newRelativePath;
    m_pathIndex[newRelativePath] = guid;
    return true;
}

const AssetMetadata* AssetDatabase::findByGUID(const AssetGUID& guid) const noexcept {
    auto it = m_assets.find(guid);
    return (it != m_assets.end()) ? &it->second : nullptr;
}

const AssetMetadata* AssetDatabase::findByPath(const std::string& relativePath) const noexcept {
    auto it = m_pathIndex.find(relativePath);
    if (it == m_pathIndex.end()) return nullptr;
    return findByGUID(it->second);
}

bool AssetDatabase::addDependency(const AssetGUID& asset, const AssetGUID& dependency) {
    auto it = m_assets.find(asset);
    if (it == m_assets.end() || !dependency.isValid()) return false;
    auto& deps = it->second.dependencies;
    if (std::find(deps.begin(), deps.end(), dependency) == deps.end()) {
        deps.push_back(dependency);
        m_reverseDeps[dependency].insert(asset);
    }
    return true;
}

std::vector<AssetGUID> AssetDatabase::getDependencies(const AssetGUID& asset) const {
    auto it = m_assets.find(asset);
    return (it != m_assets.end()) ? it->second.dependencies : std::vector<AssetGUID>{};
}

std::vector<AssetGUID> AssetDatabase::getReverseDependents(const AssetGUID& asset) const {
    auto it = m_reverseDeps.find(asset);
    if (it == m_reverseDeps.end()) return {};
    return std::vector<AssetGUID>(it->second.begin(), it->second.end());
}

bool AssetDatabase::hasCircularDependency(const AssetGUID& start) const {
    std::unordered_set<AssetGUID, AssetGUIDHash> visited;
    std::queue<AssetGUID> q;
    for (const auto& d : getDependencies(start)) q.push(d);
    while (!q.empty()) {
        const AssetGUID curr = q.front();
        q.pop();
        if (curr == start) return true;
        if (visited.insert(curr).second) {
            for (const auto& next : getDependencies(curr)) q.push(next);
        }
    }
    return false;
}

bool AssetDatabase::needsReimport(const AssetGUID& guid) const {
    const auto* meta = findByGUID(guid);
    if (!meta) return true;
    if (meta->status != AssetImportStatus::Imported) return true;
    const auto absPath = m_projectRoot.empty()
        ? std::filesystem::path(meta->relativeSourcePath)
        : (m_projectRoot / meta->relativeSourcePath);
    if (!std::filesystem::exists(absPath)) return true;
    if (computeFileContentHash(absPath) != meta->sourceHash) return true;
    if (meta->settings.computeHash() != meta->settingsHash) return true;
    const auto* imp = findImporterForExtension(toLowerExt(absPath));
    if (imp && imp->getVersion() != meta->importerVersion) return true;
    return false;
}

AssetGUID AssetDatabase::importFile(const std::string& relativeSourcePath,
                                    const std::filesystem::path& cookedOutputDir,
                                    const AssetImportSettings& settings) {
    const auto absPath = m_projectRoot.empty()
        ? std::filesystem::path(relativeSourcePath)
        : (m_projectRoot / relativeSourcePath);

    AssetGUID guid{};
    if (const auto* existing = findByPath(relativeSourcePath)) {
        guid = existing->guid;
    } else {
        guid = AssetGUID::generate();
    }

    auto& meta = m_assets[guid];
    meta.guid = guid;
    meta.relativeSourcePath = relativeSourcePath;
    meta.settings = settings;
    m_pathIndex[relativeSourcePath] = guid;

    const uint64_t currHash = computeFileContentHash(absPath);
    const uint64_t currSettingsHash = settings.computeHash();
    auto* imp = findImporterForExtension(toLowerExt(absPath));

    if (!imp) {
        meta.status = AssetImportStatus::Failed;
        meta.errorMessage = "Unsupported file extension: " + toLowerExt(absPath);
        return guid;
    }

    // Incremental skip if source hash, settings hash, and importer version are unchanged
    if (meta.status == AssetImportStatus::Imported &&
        meta.sourceHash == currHash &&
        meta.settingsHash == currSettingsHash &&
        meta.importerVersion == imp->getVersion() &&
        !meta.cookedArtifactPath.empty() &&
        std::filesystem::exists(meta.cookedArtifactPath)) {
        return guid;
    }

    meta.type = imp->getAssetType();
    meta.importerName = imp->getName();
    meta.importerVersion = imp->getVersion();
    meta.sourceHash = currHash;
    meta.settingsHash = currSettingsHash;

    AssetImportContext ctx{absPath, cookedOutputDir, meta};
    const AssetImportResult res = imp->importAsset(ctx);
    if (res.success) {
        meta.status = AssetImportStatus::Imported;
        meta.cookedArtifactPath = res.cookedArtifactPath;
        meta.errorMessage.clear();
        for (const auto& dep : res.discoveredDependencies) {
            addDependency(guid, dep);
        }
    } else {
        meta.status = AssetImportStatus::Failed;
        meta.errorMessage = res.errorMessage;
    }
    return guid;
}

std::vector<AssetValidationIssue> AssetDatabase::validateAll() const {
    std::vector<AssetValidationIssue> issues;
    for (const auto& [guid, meta] : m_assets) {
        if (meta.status == AssetImportStatus::Failed) {
            issues.push_back({guid, meta.relativeSourcePath, true, "Import", meta.errorMessage});
        }
        for (const auto& dep : meta.dependencies) {
            if (m_assets.find(dep) == m_assets.end()) {
                issues.push_back({guid, meta.relativeSourcePath, true, "Dependency", "Missing dependency GUID: " + dep.toString()});
            }
        }
        if (hasCircularDependency(guid)) {
            issues.push_back({guid, meta.relativeSourcePath, true, "Dependency", "Circular asset dependency detected."});
        }
    }
    return issues;
}

AssetCookReport AssetDatabase::cookAll(const std::filesystem::path& cookedOutputDir) {
    AssetCookReport report{};
    report.totalAssets = static_cast<uint32_t>(m_assets.size());
    for (auto& [guid, meta] : m_assets) {
        if (!needsReimport(guid) && !meta.cookedArtifactPath.empty() && std::filesystem::exists(meta.cookedArtifactPath)) {
            ++report.skippedIncremental;
            continue;
        }
        importFile(meta.relativeSourcePath, cookedOutputDir, meta.settings);
        if (meta.status == AssetImportStatus::Imported) {
            ++report.cookedAssets;
        } else {
            ++report.failedAssets;
        }
    }
    report.issues = validateAll();
    for (const auto& issue : report.issues) {
        if (issue.stage == "Dependency") ++report.missingDependencies;
    }
    saveManifestJson(cookedOutputDir / "asset_manifest.json");
    return report;
}

bool AssetDatabase::saveManifestJson(const std::filesystem::path& outPath) const {
    std::error_code ec;
    if (outPath.has_parent_path()) {
        std::filesystem::create_directories(outPath.parent_path(), ec);
    }
    std::ofstream f(outPath);
    if (!f.is_open()) return false;
    f << "{\n  \"schema_version\": " << AssetMetadata::kSchemaVersion << ",\n  \"assets\": [\n";
    size_t idx = 0;
    for (const auto& [guid, meta] : m_assets) {
        f << "    {\n"
          << "      \"guid\": \"" << guid.toString() << "\",\n"
          << "      \"type\": \"" << assetTypeName(meta.type) << "\",\n"
          << "      \"source\": \"" << meta.relativeSourcePath << "\",\n"
          << "      \"artifact\": \"" << meta.cookedArtifactPath << "\",\n"
          << "      \"source_hash\": " << meta.sourceHash << ",\n"
          << "      \"importer\": \"" << meta.importerName << "\",\n"
          << "      \"status\": " << static_cast<uint32_t>(meta.status) << "\n"
          << "    }" << (++idx < m_assets.size() ? "," : "") << "\n";
    }
    f << "  ]\n}\n";
    return true;
}

} // namespace sidera
