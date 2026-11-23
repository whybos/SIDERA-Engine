#pragma once

#include "assets/AssetTypes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace sidera {

struct AssetImportContext {
    std::filesystem::path absoluteSourcePath;
    std::filesystem::path cookedOutputDir;
    AssetMetadata& metadata;
};

struct AssetImportResult {
    bool success = false;
    std::string cookedArtifactPath;
    std::vector<AssetGUID> discoveredDependencies;
    std::string summary;
    std::string errorMessage;
};

class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;
    [[nodiscard]] virtual const char* getName() const noexcept = 0;
    [[nodiscard]] virtual uint32_t getVersion() const noexcept = 0;
    [[nodiscard]] virtual AssetType getAssetType() const noexcept = 0;
    [[nodiscard]] virtual bool supportsExtension(const std::string& lowerExt) const noexcept = 0;
    virtual AssetImportResult importAsset(const AssetImportContext& ctx) = 0;
};

// 1. OBJ Static Mesh Importer (parses vertices, normals, UVs, faces, and computes AABB bounds)
class OBJMeshImporter final : public IAssetImporter {
public:
    const char* getName() const noexcept override { return "OBJMeshImporter"; }
    uint32_t getVersion() const noexcept override { return 1; }
    AssetType getAssetType() const noexcept override { return AssetType::Mesh; }
    bool supportsExtension(const std::string& ext) const noexcept override { return ext == ".obj"; }

    AssetImportResult importAsset(const AssetImportContext& ctx) override {
        AssetImportResult res{};
        std::ifstream f(ctx.absoluteSourcePath);
        if (!f.is_open()) {
            res.errorMessage = "Failed to open OBJ source file.";
            return res;
        }
        uint32_t vCount = 0, vnCount = 0, vtCount = 0, fCount = 0;
        float minX = 1e30f, minY = 1e30f, minZ = 1e30f;
        float maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("v ", 0) == 0) {
                std::istringstream iss(line.substr(2));
                float x = 0, y = 0, z = 0;
                if (iss >> x >> y >> z) {
                    x *= ctx.metadata.settings.scaleFactor;
                    y *= ctx.metadata.settings.scaleFactor;
                    z *= ctx.metadata.settings.scaleFactor;
                    minX = std::min(minX, x); minY = std::min(minY, y); minZ = std::min(minZ, z);
                    maxX = std::max(maxX, x); maxY = std::max(maxY, y); maxZ = std::max(maxZ, z);
                    ++vCount;
                }
            } else if (line.rfind("vn ", 0) == 0) {
                ++vnCount;
            } else if (line.rfind("vt ", 0) == 0) {
                ++vtCount;
            } else if (line.rfind("f ", 0) == 0) {
                ++fCount;
            }
        }
        if (vCount == 0 || fCount == 0) {
            res.errorMessage = "OBJ mesh contains no valid vertices or faces.";
            return res;
        }

        std::error_code ec;
        std::filesystem::create_directories(ctx.cookedOutputDir, ec);
        const auto outPath = ctx.cookedOutputDir / (ctx.metadata.guid.toString() + ".smesh");
        std::ofstream out(outPath, std::ios::binary);
        if (!out.is_open()) {
            res.errorMessage = "Failed to write cooked .smesh artifact.";
            return res;
        }
        out << "SMESH1\n" << vCount << " " << vnCount << " " << vtCount << " " << fCount << "\n"
            << minX << " " << minY << " " << minZ << " " << maxX << " " << maxY << " " << maxZ << "\n";
        res.success = true;
        res.cookedArtifactPath = outPath.generic_string();
        res.summary = "OBJ vertices=" + std::to_string(vCount) + " faces=" + std::to_string(fCount);
        return res;
    }
};

// 2. Texture Importer (parses PNG IHDR / TGA / PPM headers, enforces sRGB vs linear preset)
class TextureMetadataImporter final : public IAssetImporter {
public:
    const char* getName() const noexcept override { return "TextureMetadataImporter"; }
    uint32_t getVersion() const noexcept override { return 1; }
    AssetType getAssetType() const noexcept override { return AssetType::Texture; }
    bool supportsExtension(const std::string& ext) const noexcept override {
        return ext == ".png" || ext == ".tga" || ext == ".ppm";
    }

    AssetImportResult importAsset(const AssetImportContext& ctx) override {
        AssetImportResult res{};
        std::ifstream f(ctx.absoluteSourcePath, std::ios::binary);
        if (!f.is_open()) {
            res.errorMessage = "Failed to open texture source file.";
            return res;
        }
        uint32_t width = 0, height = 0;
        const std::string ext = ctx.absoluteSourcePath.extension().string();
        if (ext == ".png") {
            uint8_t hdr[24]{};
            if (!f.read(reinterpret_cast<char*>(hdr), 24) || hdr[0] != 0x89 || hdr[1] != 'P' || hdr[2] != 'N' || hdr[3] != 'G') {
                res.errorMessage = "Corrupted or invalid PNG header.";
                return res;
            }
            width = (uint32_t(hdr[16]) << 24) | (uint32_t(hdr[17]) << 16) | (uint32_t(hdr[18]) << 8) | uint32_t(hdr[19]);
            height = (uint32_t(hdr[20]) << 24) | (uint32_t(hdr[21]) << 16) | (uint32_t(hdr[22]) << 8) | uint32_t(hdr[23]);
        } else if (ext == ".tga") {
            uint8_t hdr[18]{};
            if (!f.read(reinterpret_cast<char*>(hdr), 18)) {
                res.errorMessage = "Corrupted or truncated TGA header.";
                return res;
            }
            width = uint32_t(hdr[12]) | (uint32_t(hdr[13]) << 8);
            height = uint32_t(hdr[14]) | (uint32_t(hdr[15]) << 8);
        } else if (ext == ".ppm") {
            std::string magic;
            if (!(f >> magic >> width >> height) || (magic != "P3" && magic != "P6")) {
                res.errorMessage = "Invalid PPM header.";
                return res;
            }
        }
        if (width == 0 || height == 0) {
            res.errorMessage = "Texture dimensions must be > 0.";
            return res;
        }

        const bool srgb = isSRGBTexturePreset(ctx.metadata.settings.texturePreset);
        std::error_code ec;
        std::filesystem::create_directories(ctx.cookedOutputDir, ec);
        const auto outPath = ctx.cookedOutputDir / (ctx.metadata.guid.toString() + ".stex");
        std::ofstream out(outPath);
        out << "STEX1\n" << width << " " << height << " " << (srgb ? "sRGB" : "Linear")
            << " mips=" << (ctx.metadata.settings.generateMipmaps ? 1 : 0) << "\n";
        res.success = true;
        res.cookedArtifactPath = outPath.generic_string();
        res.summary = "Texture " + std::to_string(width) + "x" + std::to_string(height) + (srgb ? " sRGB" : " Linear");
        return res;
    }
};

// 3. WAV Audio Importer (parses RIFF/WAVE fmt header for sample rate, channels, bit depth)
class WAVAudioImporter final : public IAssetImporter {
public:
    const char* getName() const noexcept override { return "WAVAudioImporter"; }
    uint32_t getVersion() const noexcept override { return 1; }
    AssetType getAssetType() const noexcept override { return AssetType::Audio; }
    bool supportsExtension(const std::string& ext) const noexcept override { return ext == ".wav"; }

    AssetImportResult importAsset(const AssetImportContext& ctx) override {
        AssetImportResult res{};
        std::ifstream f(ctx.absoluteSourcePath, std::ios::binary);
        if (!f.is_open()) {
            res.errorMessage = "Failed to open WAV audio file.";
            return res;
        }
        uint8_t hdr[44]{};
        if (!f.read(reinterpret_cast<char*>(hdr), 44) ||
            hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F' ||
            hdr[8] != 'W' || hdr[9] != 'A' || hdr[10] != 'V' || hdr[11] != 'E') {
            res.errorMessage = "Invalid RIFF/WAVE header.";
            return res;
        }
        const uint16_t channels = uint16_t(hdr[22]) | (uint16_t(hdr[23]) << 8);
        const uint32_t sampleRate = uint32_t(hdr[24]) | (uint32_t(hdr[25]) << 8) | (uint32_t(hdr[26]) << 16) | (uint32_t(hdr[27]) << 24);
        const uint16_t bitsPerSample = uint16_t(hdr[34]) | (uint16_t(hdr[35]) << 8);
        if (channels == 0 || sampleRate == 0) {
            res.errorMessage = "Invalid WAV channel count or sample rate.";
            return res;
        }
        std::error_code ec;
        std::filesystem::create_directories(ctx.cookedOutputDir, ec);
        const auto outPath = ctx.cookedOutputDir / (ctx.metadata.guid.toString() + ".saudio");
        std::ofstream out(outPath);
        out << "SAUDIO1\n" << sampleRate << " " << channels << " " << bitsPerSample << "\n";
        res.success = true;
        res.cookedArtifactPath = outPath.generic_string();
        res.summary = "WAV " + std::to_string(sampleRate) + "Hz " + std::to_string(channels) + "ch";
        return res;
    }
};

// 4. Shader Asset Importer (parses GLSL source & #include dependencies)
class ShaderAssetImporter final : public IAssetImporter {
public:
    const char* getName() const noexcept override { return "ShaderAssetImporter"; }
    uint32_t getVersion() const noexcept override { return 1; }
    AssetType getAssetType() const noexcept override { return AssetType::Shader; }
    bool supportsExtension(const std::string& ext) const noexcept override {
        return ext == ".vert" || ext == ".frag" || ext == ".comp";
    }

    AssetImportResult importAsset(const AssetImportContext& ctx) override {
        AssetImportResult res{};
        std::ifstream f(ctx.absoluteSourcePath);
        if (!f.is_open()) {
            res.errorMessage = "Failed to open GLSL shader source.";
            return res;
        }
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (content.empty()) {
            res.errorMessage = "Shader source is empty.";
            return res;
        }
        std::error_code ec;
        std::filesystem::create_directories(ctx.cookedOutputDir, ec);
        const auto outPath = ctx.cookedOutputDir / (ctx.metadata.guid.toString() + ".sshadermeta");
        std::ofstream out(outPath);
        out << "SSHADER1\nbytes=" << content.size() << "\n";
        res.success = true;
        res.cookedArtifactPath = outPath.generic_string();
        res.summary = "GLSL Shader (" + std::to_string(content.size()) + " bytes)";
        return res;
    }
};

} // namespace sidera
