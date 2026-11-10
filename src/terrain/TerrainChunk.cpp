#include "terrain/TerrainChunk.h"
#include "terrain/HeightmapData.h"
#include "terrain/TerrainConfig.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <algorithm>
#include <format>
#include <limits>
#include <vector>

namespace sidera {

TerrainChunk::TerrainChunk(uint32_t chunkId,
                           uint32_t gridX,
                           uint32_t gridZ,
                           const TerrainConfig& config,
                           const HeightmapData& heightmap,
                           const VulkanDevice& device,
                           VkCommandPool transferCommandPool)
    : m_chunkId(chunkId)
    , m_gridX(gridX)
    , m_gridZ(gridZ) {
    generateMesh(config, heightmap, &device, transferCommandPool);
}

TerrainChunk::TerrainChunk(uint32_t chunkId,
                           uint32_t gridX,
                           uint32_t gridZ,
                           const TerrainConfig& config,
                           const HeightmapData& heightmap)
    : m_chunkId(chunkId)
    , m_gridX(gridX)
    , m_gridZ(gridZ) {
    generateMesh(config, heightmap, nullptr, VK_NULL_HANDLE);
}

bool TerrainChunk::uploadGpu(const VulkanDevice& device, VkCommandPool transferCommandPool) {
    if (!m_mesh) {
        return false;
    }
    return m_mesh->uploadToGpu(device, transferCommandPool);
}

void TerrainChunk::releaseGpu() noexcept {
    if (m_mesh) {
        m_mesh->releaseGpuBuffers();
    }
}

bool TerrainChunk::isGpuResident() const noexcept {
    return m_mesh && m_mesh->isGpuResident();
}

uint64_t TerrainChunk::getGpuMemoryUsageBytes() const noexcept {
    return m_mesh ? m_mesh->getGpuMemoryUsageBytes() : 0;
}

uint64_t TerrainChunk::getCpuMemoryUsageBytes() const noexcept {
    return sizeof(TerrainChunk) + (m_mesh ? m_mesh->getCpuMemoryUsageBytes() : 0);
}

void TerrainChunk::bind(VkCommandBuffer cmd) const noexcept {
    if (m_mesh) {
        m_mesh->bind(cmd);
    }
}

void TerrainChunk::draw(VkCommandBuffer cmd) const noexcept {
    if (m_mesh) {
        m_mesh->draw(cmd);
    }
}

void TerrainChunk::generateMesh(const TerrainConfig& config,
                                const HeightmapData& heightmap,
                                const VulkanDevice* device,
                                VkCommandPool transferCommandPool) {
    const uint32_t R = std::max(2u, config.chunkResolution);
    const uint32_t totalQuadsX = std::max(1u, config.chunksX * R);
    const uint32_t totalQuadsZ = std::max(1u, config.chunksZ * R);

    const float invTotalQuadsX = 1.0f / static_cast<float>(totalQuadsX);
    const float invTotalQuadsZ = 1.0f / static_cast<float>(totalQuadsZ);

    const uint32_t vertexCountAcross = R + 1;
    const uint32_t totalVertices = vertexCountAcross * vertexCountAcross;

    std::vector<Vertex> vertices;
    vertices.reserve(totalVertices);

    float minX = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();

    for (uint32_t iz = 0; iz <= R; ++iz) {
        const uint32_t globalQuadZ = m_gridZ * R + iz;
        const float v = static_cast<float>(globalQuadZ) * invTotalQuadsZ;
        const float worldZ = (v - 0.5f) * config.length;

        for (uint32_t ix = 0; ix <= R; ++ix) {
            const uint32_t globalQuadX = m_gridX * R + ix;
            const float u = static_cast<float>(globalQuadX) * invTotalQuadsX;
            const float worldX = (u - 0.5f) * config.width;

            // Height sampled from continuous heightmap
            const float normH = heightmap.sampleBilinear(u, v);
            const float worldY = config.heightOffset + normH * config.heightScale;

            // Normal calculated directly from continuous heightmap ensuring seam-free chunk borders
            const glm::vec3 normal = heightmap.computeNormal(u, v, config.width, config.length, config.heightScale);

            // UV for high-frequency detail PBR texturing
            const glm::vec2 uv(u * config.textureTiling, v * config.textureTiling);

            // Vertex color encodes (u, v, normalizedHeight)
            const glm::vec3 color(u, v, normH);

            vertices.push_back(Vertex{
                .position = glm::vec3(worldX, worldY, worldZ),
                .color = color,
                .uv = uv,
                .normal = normal
            });

            minX = std::min(minX, worldX);
            maxX = std::max(maxX, worldX);
            minY = std::min(minY, worldY);
            maxY = std::max(maxY, worldY);
            minZ = std::min(minZ, worldZ);
            maxZ = std::max(maxZ, worldZ);
        }
    }

    // Index buffer generation with Counter-Clockwise (CCW) triangle winding
    const uint32_t totalIndices = R * R * 6;
    std::vector<uint32_t> indices;
    indices.reserve(totalIndices);

    for (uint32_t iz = 0; iz < R; ++iz) {
        for (uint32_t ix = 0; ix < R; ++ix) {
            const uint32_t v00 = iz * vertexCountAcross + ix;
            const uint32_t v10 = iz * vertexCountAcross + (ix + 1);
            const uint32_t v01 = (iz + 1) * vertexCountAcross + ix;
            const uint32_t v11 = (iz + 1) * vertexCountAcross + (ix + 1);

            // Triangle 1 (CCW)
            indices.push_back(v00);
            indices.push_back(v01);
            indices.push_back(v10);

            // Triangle 2 (CCW)
            indices.push_back(v10);
            indices.push_back(v01);
            indices.push_back(v11);
        }
    }

    const std::string name = std::format("TerrainChunk_{}_{}", m_gridX, m_gridZ);
    if (device && transferCommandPool != VK_NULL_HANDLE) {
        m_mesh = std::make_unique<Mesh>(*device, transferCommandPool, vertices, indices, name);
    } else {
        m_mesh = std::make_unique<Mesh>(vertices, indices, name);
    }

    m_minHeight = minY;
    m_maxHeight = maxY;
    m_worldAabb.min = glm::vec3(minX, minY, minZ);
    m_worldAabb.max = glm::vec3(maxX, maxY, maxZ);
}

} // namespace sidera
