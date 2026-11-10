#pragma once

#include "renderer/TextureManager.h"
#include "renderer/vulkan/VulkanTexture.h"
#include "renderer/vulkan/VulkanTextureArray.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan.h>

#include <memory>
#include <string>
#include <vector>

namespace sidera {

class HeightmapData;
struct TerrainConfig;

// Parameters for each individual terrain material layer
struct TerrainLayerProperties {
    std::string name = "Layer";
    glm::vec3 tint{1.0f};
    float roughness = 0.7f;
    float metallic = 0.0f;
    float ao = 1.0f;
    float normalScale = 1.0f;
};

class TerrainMaterial {
public:
    TerrainMaterial(const VulkanDevice& device,
                    VkCommandPool transferCommandPool,
                    TextureManager& textureManager);
    ~TerrainMaterial() = default;

    TerrainMaterial(const TerrainMaterial&) = delete;
    TerrainMaterial& operator=(const TerrainMaterial&) = delete;
    TerrainMaterial(TerrainMaterial&&) noexcept = default;
    TerrainMaterial& operator=(TerrainMaterial&&) noexcept = default;

    // Build or generate multi-layer textures (Grass, Rock, Dirt, Sand) and Splat map
    void init(const TerrainConfig& config, const HeightmapData& heightmap);

    // Load custom splat map from file
    bool loadSplatMap(const std::string& filepath);

    [[nodiscard]] std::shared_ptr<VulkanTexture> getSplatMap() const noexcept { return m_splatMap; }
    [[nodiscard]] std::shared_ptr<VulkanTextureArray> getAlbedoArray() const noexcept { return m_albedoArray; }
    [[nodiscard]] std::shared_ptr<VulkanTextureArray> getNormalArray() const noexcept { return m_normalArray; }
    [[nodiscard]] std::shared_ptr<VulkanTextureArray> getRoughAoArray() const noexcept { return m_roughAoArray; }

    [[nodiscard]] const std::vector<TerrainLayerProperties>& getLayers() const noexcept { return m_layers; }

private:
    void generateProceduralLayers();
    void generateProceduralSplatMap(const TerrainConfig& config, const HeightmapData& heightmap);

    const VulkanDevice& m_device;
    VkCommandPool m_transferCommandPool;
    TextureManager& m_textureManager;

    std::vector<TerrainLayerProperties> m_layers;
    std::shared_ptr<VulkanTexture> m_splatMap;
    std::shared_ptr<VulkanTextureArray> m_albedoArray;
    std::shared_ptr<VulkanTextureArray> m_normalArray;
    std::shared_ptr<VulkanTextureArray> m_roughAoArray;
};

} // namespace sidera
