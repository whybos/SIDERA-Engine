#pragma once

#include "renderer/BoundingVolume.h"
#include "renderer/Mesh.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace sidera {

class VulkanDevice;
class HeightmapData;
struct TerrainConfig;

class TerrainChunk {
public:
    TerrainChunk(uint32_t chunkId,
                 uint32_t gridX,
                 uint32_t gridZ,
                 const TerrainConfig& config,
                 const HeightmapData& heightmap,
                 const VulkanDevice& device,
                 VkCommandPool transferCommandPool);
    TerrainChunk(uint32_t chunkId,
                 uint32_t gridX,
                 uint32_t gridZ,
                 const TerrainConfig& config,
                 const HeightmapData& heightmap);
    ~TerrainChunk() = default;

    TerrainChunk(const TerrainChunk&) = delete;
    TerrainChunk& operator=(const TerrainChunk&) = delete;
    TerrainChunk(TerrainChunk&&) noexcept = default;
    TerrainChunk& operator=(TerrainChunk&&) noexcept = default;

    void bind(VkCommandBuffer cmd) const noexcept;
    void draw(VkCommandBuffer cmd) const noexcept;

    // GPU buffer lifecycle for streaming
    bool uploadGpu(const VulkanDevice& device, VkCommandPool transferCommandPool);
    void releaseGpu() noexcept;
    [[nodiscard]] bool isGpuResident() const noexcept;
    [[nodiscard]] uint64_t getGpuMemoryUsageBytes() const noexcept;
    [[nodiscard]] uint64_t getCpuMemoryUsageBytes() const noexcept;

    [[nodiscard]] uint32_t getChunkId() const noexcept { return m_chunkId; }
    [[nodiscard]] uint32_t getId() const noexcept { return m_chunkId; }
    [[nodiscard]] uint32_t getGridX() const noexcept { return m_gridX; }
    [[nodiscard]] uint32_t getGridZ() const noexcept { return m_gridZ; }
    [[nodiscard]] const Mesh* getMesh() const noexcept { return m_mesh.get(); }
    [[nodiscard]] const glm::mat4& getWorldTransform() const noexcept { return m_worldTransform; }
    [[nodiscard]] const AABB& getBoundingBox() const noexcept { return m_worldAabb; }
    [[nodiscard]] const AABB& getBounds() const noexcept { return m_worldAabb; }
    [[nodiscard]] float getMinHeight() const noexcept { return m_minHeight; }
    [[nodiscard]] float getMaxHeight() const noexcept { return m_maxHeight; }
    [[nodiscard]] bool isVisible() const noexcept { return m_isVisible; }
    void setVisible(bool visible) noexcept { m_isVisible = visible; }

    [[nodiscard]] uint32_t getVertexCount() const noexcept { return m_mesh ? m_mesh->getVertexCount() : 0; }
    [[nodiscard]] uint32_t getTriangleCount() const noexcept { return m_mesh ? m_mesh->getIndexCount() / 3 : 0; }

private:
    void generateMesh(const TerrainConfig& config,
                      const HeightmapData& heightmap,
                      const VulkanDevice* device,
                      VkCommandPool transferCommandPool);

    uint32_t m_chunkId = 0;
    uint32_t m_gridX = 0;
    uint32_t m_gridZ = 0;
    glm::mat4 m_worldTransform{1.0f};
    AABB m_worldAabb{};
    float m_minHeight = 0.0f;
    float m_maxHeight = 0.0f;
    bool m_isVisible = true;

    std::unique_ptr<Mesh> m_mesh;
};

} // namespace sidera
