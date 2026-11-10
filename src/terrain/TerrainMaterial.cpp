#include "terrain/TerrainMaterial.h"
#include "core/Log.h"
#include "terrain/HeightmapData.h"
#include "terrain/TerrainConfig.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

namespace sidera {

TerrainMaterial::TerrainMaterial(const VulkanDevice& device,
                                 VkCommandPool transferCommandPool,
                                 TextureManager& textureManager)
    : m_device(device)
    , m_transferCommandPool(transferCommandPool)
    , m_textureManager(textureManager) {
    m_layers = {
        TerrainLayerProperties{.name = "Grass", .tint = {0.28f, 0.54f, 0.20f}, .roughness = 0.78f, .metallic = 0.0f, .ao = 0.95f},
        TerrainLayerProperties{.name = "Rock",  .tint = {0.46f, 0.44f, 0.42f}, .roughness = 0.62f, .metallic = 0.04f, .ao = 0.85f},
        TerrainLayerProperties{.name = "Dirt",  .tint = {0.42f, 0.30f, 0.20f}, .roughness = 0.88f, .metallic = 0.0f, .ao = 0.90f},
        TerrainLayerProperties{.name = "Sand",  .tint = {0.82f, 0.74f, 0.54f}, .roughness = 0.65f, .metallic = 0.0f, .ao = 1.0f}
    };
}

void TerrainMaterial::init(const TerrainConfig& config, const HeightmapData& heightmap) {
    SIDERA_LOG_INFO("TerrainMaterial", "Initializing multi-layer terrain materials (4 layers: Grass, Rock, Dirt, Sand)...");
    generateProceduralLayers();

    if (!config.splatMapPath.empty() && loadSplatMap(config.splatMapPath)) {
        SIDERA_LOG_INFO("TerrainMaterial", "Loaded custom terrain splat map from '{}'.", config.splatMapPath);
    } else {
        generateProceduralSplatMap(config, heightmap);
    }
}

bool TerrainMaterial::loadSplatMap(const std::string& filepath) {
    if (!std::filesystem::exists(filepath)) {
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    uint8_t* pixels = stbi_load(filepath.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        return false;
    }

    m_splatMap = m_textureManager.createTexture(
        "terrain_splat_custom",
        pixels,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        VK_FORMAT_R8G8B8A8_UNORM,
        true
    );

    stbi_image_free(pixels);
    return m_splatMap != nullptr;
}

void TerrainMaterial::generateProceduralLayers() {
    constexpr uint32_t res = 256;
    constexpr size_t pixelCount = res * res;
    constexpr size_t byteCount = pixelCount * 4;

    std::vector<uint8_t> grassAlbedo(byteCount), rockAlbedo(byteCount), dirtAlbedo(byteCount), sandAlbedo(byteCount);
    std::vector<uint8_t> grassNormal(byteCount), rockNormal(byteCount), dirtNormal(byteCount), sandNormal(byteCount);
    std::vector<uint8_t> grassRoughAo(byteCount), rockRoughAo(byteCount), dirtRoughAo(byteCount), sandRoughAo(byteCount);

    for (uint32_t y = 0; y < res; ++y) {
        for (uint32_t x = 0; x < res; ++x) {
            const size_t idx = (y * res + x) * 4;
            const float u = static_cast<float>(x) / static_cast<float>(res);
            const float v = static_cast<float>(y) / static_cast<float>(res);

            // Pseudo-noise variations
            const float n1 = std::sin(u * 32.0f) * std::cos(v * 32.0f);
            const float n2 = std::sin(u * 64.0f + 1.2f) * std::cos(v * 64.0f + 2.4f);
            const float n3 = std::sin(u * 128.0f) * std::sin(v * 128.0f);

            // -------------------------------------------------------------
            // 0. GRASS LAYER
            // -------------------------------------------------------------
            {
                const float gVar = 0.85f + 0.15f * n1 + 0.08f * n2;
                grassAlbedo[idx + 0] = static_cast<uint8_t>(std::clamp(62.0f * gVar, 0.0f, 255.0f));
                grassAlbedo[idx + 1] = static_cast<uint8_t>(std::clamp(138.0f * gVar, 0.0f, 255.0f));
                grassAlbedo[idx + 2] = static_cast<uint8_t>(std::clamp(48.0f * gVar, 0.0f, 255.0f));
                grassAlbedo[idx + 3] = 255;

                // Fine organic grass normal map
                const float gnx = n2 * 0.25f;
                const float gny = n3 * 0.25f;
                grassNormal[idx + 0] = static_cast<uint8_t>((gnx * 0.5f + 0.5f) * 255.0f);
                grassNormal[idx + 1] = static_cast<uint8_t>((gny * 0.5f + 0.5f) * 255.0f);
                grassNormal[idx + 2] = 255;
                grassNormal[idx + 3] = 255;

                // R=AO, G=Roughness, B=Metallic, A=Pad
                grassRoughAo[idx + 0] = static_cast<uint8_t>(0.95f * 255.0f); // AO
                grassRoughAo[idx + 1] = static_cast<uint8_t>(0.80f * 255.0f); // Roughness
                grassRoughAo[idx + 2] = 0;                                    // Metallic
                grassRoughAo[idx + 3] = 255;
            }

            // -------------------------------------------------------------
            // 1. ROCK LAYER (Granite / Basalt slate)
            // -------------------------------------------------------------
            {
                const float rPattern = std::abs(std::sin(u * 40.0f + v * 30.0f) + n2 * 0.4f);
                const float rVar = 0.70f + 0.30f * rPattern;
                rockAlbedo[idx + 0] = static_cast<uint8_t>(std::clamp(118.0f * rVar, 0.0f, 255.0f));
                rockAlbedo[idx + 1] = static_cast<uint8_t>(std::clamp(112.0f * rVar, 0.0f, 255.0f));
                rockAlbedo[idx + 2] = static_cast<uint8_t>(std::clamp(108.0f * rVar, 0.0f, 255.0f));
                rockAlbedo[idx + 3] = 255;

                // Rugged angular stone normals
                const float rnx = std::cos(u * 40.0f + v * 30.0f) * 0.45f;
                const float rny = std::sin(u * 40.0f + v * 30.0f) * 0.45f;
                rockNormal[idx + 0] = static_cast<uint8_t>((rnx * 0.5f + 0.5f) * 255.0f);
                rockNormal[idx + 1] = static_cast<uint8_t>((rny * 0.5f + 0.5f) * 255.0f);
                rockNormal[idx + 2] = 255;
                rockNormal[idx + 3] = 255;

                rockRoughAo[idx + 0] = static_cast<uint8_t>(0.85f * 255.0f); // AO
                rockRoughAo[idx + 1] = static_cast<uint8_t>(0.58f * 255.0f); // Roughness
                rockRoughAo[idx + 2] = static_cast<uint8_t>(0.05f * 255.0f); // Metallic
                rockRoughAo[idx + 3] = 255;
            }

            // -------------------------------------------------------------
            // 2. DIRT LAYER (Rich loamy soil with speckles)
            // -------------------------------------------------------------
            {
                const float dVar = 0.82f + 0.18f * (n1 * 0.6f + n3 * 0.4f);
                dirtAlbedo[idx + 0] = static_cast<uint8_t>(std::clamp(105.0f * dVar, 0.0f, 255.0f));
                dirtAlbedo[idx + 1] = static_cast<uint8_t>(std::clamp(75.0f * dVar, 0.0f, 255.0f));
                dirtAlbedo[idx + 2] = static_cast<uint8_t>(std::clamp(48.0f * dVar, 0.0f, 255.0f));
                dirtAlbedo[idx + 3] = 255;

                const float dnx = n1 * 0.30f;
                const float dny = n2 * 0.30f;
                dirtNormal[idx + 0] = static_cast<uint8_t>((dnx * 0.5f + 0.5f) * 255.0f);
                dirtNormal[idx + 1] = static_cast<uint8_t>((dny * 0.5f + 0.5f) * 255.0f);
                dirtNormal[idx + 2] = 255;
                dirtNormal[idx + 3] = 255;

                dirtRoughAo[idx + 0] = static_cast<uint8_t>(0.90f * 255.0f); // AO
                dirtRoughAo[idx + 1] = static_cast<uint8_t>(0.88f * 255.0f); // Roughness
                dirtRoughAo[idx + 2] = 0;                                    // Metallic
                dirtRoughAo[idx + 3] = 255;
            }

            // -------------------------------------------------------------
            // 3. SAND LAYER (Warm dune sand with ripple waves)
            // -------------------------------------------------------------
            {
                const float sWave = std::sin(u * 48.0f + v * 16.0f) * 0.08f;
                const float sVar = 0.92f + sWave + n2 * 0.05f;
                sandAlbedo[idx + 0] = static_cast<uint8_t>(std::clamp(208.0f * sVar, 0.0f, 255.0f));
                sandAlbedo[idx + 1] = static_cast<uint8_t>(std::clamp(188.0f * sVar, 0.0f, 255.0f));
                sandAlbedo[idx + 2] = static_cast<uint8_t>(std::clamp(138.0f * sVar, 0.0f, 255.0f));
                sandAlbedo[idx + 3] = 255;

                // Gentle ripple normals
                const float snx = std::cos(u * 48.0f + v * 16.0f) * 0.18f;
                const float sny = std::sin(u * 48.0f + v * 16.0f) * 0.06f;
                sandNormal[idx + 0] = static_cast<uint8_t>((snx * 0.5f + 0.5f) * 255.0f);
                sandNormal[idx + 1] = static_cast<uint8_t>((sny * 0.5f + 0.5f) * 255.0f);
                sandNormal[idx + 2] = 255;
                sandNormal[idx + 3] = 255;

                sandRoughAo[idx + 0] = static_cast<uint8_t>(1.0f * 255.0f);  // AO
                sandRoughAo[idx + 1] = static_cast<uint8_t>(0.66f * 255.0f); // Roughness
                sandRoughAo[idx + 2] = 0;                                    // Metallic
                sandRoughAo[idx + 3] = 255;
            }
        }
    }

    const std::vector<const void*> albedoLayers = {grassAlbedo.data(), rockAlbedo.data(), dirtAlbedo.data(), sandAlbedo.data()};
    m_albedoArray = m_textureManager.createTextureArray("terrain_layer_albedo", albedoLayers, res, res, VK_FORMAT_R8G8B8A8_SRGB, true);

    const std::vector<const void*> normalLayers = {grassNormal.data(), rockNormal.data(), dirtNormal.data(), sandNormal.data()};
    m_normalArray = m_textureManager.createTextureArray("terrain_layer_normal", normalLayers, res, res, VK_FORMAT_R8G8B8A8_UNORM, true);

    const std::vector<const void*> roughAoLayers = {grassRoughAo.data(), rockRoughAo.data(), dirtRoughAo.data(), sandRoughAo.data()};
    m_roughAoArray = m_textureManager.createTextureArray("terrain_layer_rough_ao", roughAoLayers, res, res, VK_FORMAT_R8G8B8A8_UNORM, true);

    SIDERA_LOG_INFO("TerrainMaterial", "Terrain 4-layer PBR texture arrays successfully built (256x256 per layer, mipmapped).");
}

void TerrainMaterial::generateProceduralSplatMap(const TerrainConfig& config, const HeightmapData& heightmap) {
    constexpr uint32_t splatRes = 512;
    std::vector<uint8_t> splatPixels(splatRes * splatRes * 4);

    const float invRes = 1.0f / static_cast<float>(splatRes - 1);

    for (uint32_t y = 0; y < splatRes; ++y) {
        const float v = static_cast<float>(y) * invRes;
        for (uint32_t x = 0; x < splatRes; ++x) {
            const float u = static_cast<float>(x) * invRes;

            const float h = heightmap.sampleBilinear(u, v);
            const float slope = heightmap.computeSlope(u, v, config.width, config.length, config.heightScale);

            // Natural biome height & slope heuristics:
            // Rock: high slopes (> 25-30 deg) and rocky high peaks (> 0.72 elevation)
            float wRock = std::clamp((slope - 0.18f) / 0.18f, 0.0f, 1.0f) +
                          std::clamp((h - 0.72f) / 0.20f, 0.0f, 1.0f);

            // Sand: low basins (< 0.18 elevation) with gentle slope
            float wSand = std::clamp((0.18f - h) / 0.12f, 0.0f, 1.0f) *
                          (1.0f - std::clamp((slope - 0.15f) / 0.15f, 0.0f, 1.0f));

            // Dirt: pathing, transitions and moderate slopes
            float wDirt = std::clamp(1.0f - std::abs(h - 0.40f) / 0.25f, 0.0f, 1.0f) * 0.45f +
                          std::clamp((slope - 0.10f) / 0.15f, 0.0f, 0.40f);

            // Grass: lush gentle plains across mid-elevations
            float wGrass = std::clamp(1.0f - wRock - wSand - wDirt * 0.6f, 0.0f, 1.0f) *
                           (1.0f - std::clamp((slope - 0.16f) / 0.18f, 0.0f, 1.0f));

            // Strict weight normalization (wGrass + wRock + wDirt + wSand = 1.0)
            const float sum = wGrass + wRock + wDirt + wSand;
            if (sum > 0.0001f) {
                const float invSum = 1.0f / sum;
                wGrass *= invSum;
                wRock  *= invSum;
                wDirt  *= invSum;
                wSand  *= invSum;
            } else {
                wGrass = 1.0f;
                wRock = wDirt = wSand = 0.0f;
            }

            const size_t idx = (static_cast<size_t>(y) * splatRes + x) * 4;
            splatPixels[idx + 0] = static_cast<uint8_t>(std::clamp(wGrass * 255.0f, 0.0f, 255.0f)); // R: Grass
            splatPixels[idx + 1] = static_cast<uint8_t>(std::clamp(wRock  * 255.0f, 0.0f, 255.0f)); // G: Rock
            splatPixels[idx + 2] = static_cast<uint8_t>(std::clamp(wDirt  * 255.0f, 0.0f, 255.0f)); // B: Dirt
            splatPixels[idx + 3] = static_cast<uint8_t>(std::clamp(wSand  * 255.0f, 0.0f, 255.0f)); // A: Sand
        }
    }

    m_splatMap = m_textureManager.createTexture(
        "terrain_splat_procedural",
        splatPixels.data(),
        splatRes,
        splatRes,
        VK_FORMAT_R8G8B8A8_UNORM,
        true
    );

    SIDERA_LOG_INFO("TerrainMaterial", "Procedural RGBA splat weight map generated ({}x{}, normalized weights).",
                    splatRes, splatRes);
}

} // namespace sidera
