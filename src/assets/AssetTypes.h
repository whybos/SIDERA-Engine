#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace sidera {

struct AssetGUID {
    uint64_t high = 0;
    uint64_t low = 0;

    [[nodiscard]] bool isValid() const noexcept { return high != 0 || low != 0; }
    bool operator==(const AssetGUID& o) const noexcept { return high == o.high && low == o.low; }
    bool operator!=(const AssetGUID& o) const noexcept { return !(*this == o); }
    bool operator<(const AssetGUID& o) const noexcept { return (high != o.high) ? (high < o.high) : (low < o.low); }

    [[nodiscard]] std::string toString() const {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::setw(16) << high << std::setw(16) << low;
        return oss.str();
    }

    [[nodiscard]] static AssetGUID fromString(const std::string& s) noexcept {
        if (s.size() != 32) return {};
        try {
            AssetGUID g{};
            g.high = std::stoull(s.substr(0, 16), nullptr, 16);
            g.low = std::stoull(s.substr(16, 16), nullptr, 16);
            return g;
        } catch (...) {
            return {};
        }
    }

    [[nodiscard]] static AssetGUID generate() {
        static thread_local std::mt19937_64 rng(std::random_device{}());
        AssetGUID g{};
        do {
            g.high = rng();
            g.low = rng();
        } while (!g.isValid());
        return g;
    }
};

struct AssetGUIDHash {
    size_t operator()(const AssetGUID& g) const noexcept {
        return static_cast<size_t>(g.high ^ (g.low * 0x9E3779B97F4A7C15ull));
    }
};

enum class AssetType : uint32_t {
    Unknown = 0,
    Mesh = 1,
    Texture = 2,
    Material = 3,
    Shader = 4,
    Skeleton = 5,
    Animation = 6,
    Audio = 7,
    Cubemap = 8,
    Font = 9,
    Generic = 10
};

inline const char* assetTypeName(AssetType t) noexcept {
    switch (t) {
        case AssetType::Mesh:      return "Mesh";
        case AssetType::Texture:   return "Texture";
        case AssetType::Material:  return "Material";
        case AssetType::Shader:    return "Shader";
        case AssetType::Skeleton:  return "Skeleton";
        case AssetType::Animation: return "Animation";
        case AssetType::Audio:     return "Audio";
        case AssetType::Cubemap:   return "Cubemap";
        case AssetType::Font:      return "Font";
        case AssetType::Generic:   return "Generic";
        default:                   return "Unknown";
    }
}

enum class AssetImportStatus : uint32_t {
    NotImported = 0,
    Imported = 1,
    NeedsReimport = 2,
    Failed = 3
};

enum class TextureImportPreset : uint32_t {
    AlbedoSRGB = 0,
    NormalLinear = 1,
    RoughnessLinear = 2,
    MetallicLinear = 3,
    AOLinear = 4,
    EmissiveSRGB = 5,
    HDREnvironment = 6,
    UITexture = 7,
    GenericData = 8
};

inline bool isSRGBTexturePreset(TextureImportPreset p) noexcept {
    return p == TextureImportPreset::AlbedoSRGB ||
           p == TextureImportPreset::EmissiveSRGB ||
           p == TextureImportPreset::UITexture;
}

inline uint64_t computeFNV1a64(const void* data, size_t size) noexcept {
    uint64_t h = 14695981039346656037ull;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= 1099511628211ull;
    }
    return h;
}

inline uint64_t computeFileContentHash(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return 0;
    uint64_t h = 14695981039346656037ull;
    char buf[4096];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        const auto n = static_cast<size_t>(f.gcount());
        for (size_t i = 0; i < n; ++i) {
            h ^= static_cast<uint64_t>(static_cast<uint8_t>(buf[i]));
            h *= 1099511628211ull;
        }
    }
    return h;
}

struct AssetImportSettings {
    TextureImportPreset texturePreset = TextureImportPreset::AlbedoSRGB;
    bool generateMipmaps = true;
    bool generateNormals = true;
    bool generateTangents = true;
    float scaleFactor = 1.0f;

    [[nodiscard]] uint64_t computeHash() const noexcept {
        return computeFNV1a64(this, sizeof(AssetImportSettings));
    }
};

struct AssetMetadata {
    static constexpr uint32_t kSchemaVersion = 1;

    uint32_t schemaVersion = kSchemaVersion;
    AssetGUID guid{};
    AssetType type = AssetType::Unknown;
    std::string importerName;
    uint32_t importerVersion = 1;
    std::string relativeSourcePath;
    std::string cookedArtifactPath;
    uint64_t sourceHash = 0;
    uint64_t settingsHash = 0;
    AssetImportStatus status = AssetImportStatus::NotImported;
    AssetImportSettings settings{};
    std::vector<AssetGUID> dependencies;
    std::string errorMessage;
};

struct AssetValidationIssue {
    AssetGUID guid{};
    std::string sourcePath;
    bool isError = true;
    std::string stage;
    std::string message;
};

struct AssetCookReport {
    uint32_t totalAssets = 0;
    uint32_t cookedAssets = 0;
    uint32_t skippedIncremental = 0;
    uint32_t failedAssets = 0;
    uint32_t missingDependencies = 0;
    std::vector<AssetValidationIssue> issues;
};

} // namespace sidera
