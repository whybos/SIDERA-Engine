#pragma once

#include "renderer/BoundingVolume.h"
#include "renderer/Vertex.h"
#include "renderer/vulkan/VulkanBuffer.h"
#include "terrain/TerrainNeighborData.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sidera {

class VulkanDevice;
class HeightmapData;
struct TerrainConfig;

// Shared pool of 16 stitch-pattern index buffers (one for each 4-bit N/S/E/W coarse-neighbor mask).
// Shared across all terrain patches of the same grid resolution to avoid redundant GPU allocations.
class TerrainStitchIndexPool {
public:
    TerrainStitchIndexPool() = default;
    ~TerrainStitchIndexPool() = default;

    TerrainStitchIndexPool(const TerrainStitchIndexPool&) = delete;
    TerrainStitchIndexPool& operator=(const TerrainStitchIndexPool&) = delete;

    // Build all 16 CPU index patterns (and optionally upload to GPU if device != nullptr)
    void init(uint32_t patchResolution,
              const VulkanDevice* device = nullptr,
              VkCommandPool transferCommandPool = VK_NULL_HANDLE);
    void destroy() noexcept;

    [[nodiscard]] uint32_t getPatchResolution() const noexcept { return m_patchResolution; }
    [[nodiscard]] const std::vector<uint32_t>& getCpuIndices(uint8_t stitchMask) const noexcept {
        return m_cpuIndices[stitchMask & 0x0Fu];
    }
    [[nodiscard]] uint32_t getIndexCount(uint8_t stitchMask) const noexcept {
        return static_cast<uint32_t>(m_cpuIndices[stitchMask & 0x0Fu].size());
    }
    [[nodiscard]] uint32_t getTriangleCount(uint8_t stitchMask) const noexcept {
        return getIndexCount(stitchMask) / 3u;
    }
    [[nodiscard]] const VulkanBuffer* getIndexBuffer(uint8_t stitchMask) const noexcept {
        return m_gpuBuffers[stitchMask & 0x0Fu].get();
    }
    [[nodiscard]] bool isGpuResident() const noexcept { return m_gpuBuffers[0] != nullptr; }
    [[nodiscard]] uint64_t getGpuMemoryBytes() const noexcept { return m_gpuMemoryBytes; }

    // Pure CPU generator for testing & topology verification
    [[nodiscard]] static std::vector<uint32_t> generateStitchIndices(uint32_t patchResolution, uint8_t stitchMask);

private:
    uint32_t m_patchResolution = 16;
    std::array<std::vector<uint32_t>, 16> m_cpuIndices{};
    std::array<std::unique_ptr<VulkanBuffer>, 16> m_gpuBuffers{};
    uint64_t m_gpuMemoryBytes = 0;
};

// Individual adaptive terrain patch holding world-space vertices (including parent surface height for GPU geomorphing).
// Shares index buffers from TerrainStitchIndexPool.
class TerrainPatchMesh {
public:
    TerrainPatchMesh() = default;
    ~TerrainPatchMesh() = default;

    TerrainPatchMesh(const TerrainPatchMesh&) = delete;
    TerrainPatchMesh& operator=(const TerrainPatchMesh&) = delete;
    TerrainPatchMesh(TerrainPatchMesh&&) noexcept = default;
    TerrainPatchMesh& operator=(TerrainPatchMesh&&) noexcept = default;

    // Generate vertices for normalized UV domain [uMin, uMax] x [vMin, vMax] from global HeightmapData.
    // Vertex::color.x stores parentWorldY for exact GPU geomorphing.
    // Vertex::color.y stores normalized lodLevel.
    // Vertex::color.z stores 1.0 if odd vertex (morphable), 0.0 if even vertex (shared with parent).
    void generate(uint32_t patchResolution,
                  float uMin, float uMax,
                  float vMin, float vMax,
                  uint32_t lodLevel,
                  const TerrainConfig& config,
                  const HeightmapData& heightmap,
                  const VulkanDevice* device = nullptr,
                  VkCommandPool transferCommandPool = VK_NULL_HANDLE);

    bool uploadToGpu(const VulkanDevice& device, VkCommandPool transferCommandPool);
    void releaseGpu() noexcept;

    void bindAndDraw(VkCommandBuffer cmd,
                     const TerrainStitchIndexPool& indexPool,
                     uint8_t stitchMask) const noexcept;

    [[nodiscard]] bool isGpuResident() const noexcept { return m_vertexBuffer != nullptr; }
    [[nodiscard]] uint32_t getVertexCount() const noexcept { return static_cast<uint32_t>(m_cpuVertices.size()); }
    [[nodiscard]] const std::vector<Vertex>& getCpuVertices() const noexcept { return m_cpuVertices; }
    [[nodiscard]] const AABB& getBounds() const noexcept { return m_bounds; }
    [[nodiscard]] float getMinHeight() const noexcept { return m_minHeight; }
    [[nodiscard]] float getMaxHeight() const noexcept { return m_maxHeight; }
    [[nodiscard]] float getGeometricError() const noexcept { return m_geometricError; }
    [[nodiscard]] uint64_t getGpuMemoryBytes() const noexcept {
        return m_vertexBuffer ? static_cast<uint64_t>(m_vertexBuffer->getSize()) : 0ULL;
    }

private:
    std::vector<Vertex> m_cpuVertices;
    std::unique_ptr<VulkanBuffer> m_vertexBuffer;
    AABB m_bounds{};
    float m_minHeight = 0.0f;
    float m_maxHeight = 0.0f;
    float m_geometricError = 0.0f;
};

} // namespace sidera
