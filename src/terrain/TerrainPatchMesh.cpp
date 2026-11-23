#include "terrain/TerrainPatchMesh.h"
#include "terrain/HeightmapData.h"
#include "terrain/TerrainConfig.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sidera {

std::vector<uint32_t> TerrainStitchIndexPool::generateStitchIndices(uint32_t patchResolution, uint8_t stitchMask) {
    const uint32_t R = (patchResolution < 4u) ? 4u : ((patchResolution & 1u) ? patchResolution + 1u : patchResolution);
    const uint32_t stride = R + 1u;
    const auto vid = [stride](uint32_t ix, uint32_t iz) noexcept -> uint32_t {
        return iz * stride + ix;
    };

    const bool northCoarse = (stitchMask & kEdgeNorth) != 0u;
    const bool southCoarse = (stitchMask & kEdgeSouth) != 0u;
    const bool westCoarse  = (stitchMask & kEdgeWest)  != 0u;
    const bool eastCoarse  = (stitchMask & kEdgeEast)  != 0u;

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(R) * R * 6u);

    auto emitQuad = [&](uint32_t ix, uint32_t iz) {
        const uint32_t v00 = vid(ix, iz);
        const uint32_t v10 = vid(ix + 1u, iz);
        const uint32_t v01 = vid(ix, iz + 1u);
        const uint32_t v11 = vid(ix + 1u, iz + 1u);
        // Triangle 1 (CCW, +Y normal)
        indices.push_back(v00);
        indices.push_back(v01);
        indices.push_back(v10);
        // Triangle 2 (CCW, +Y normal)
        indices.push_back(v10);
        indices.push_back(v01);
        indices.push_back(v11);
    };

    // 1. Interior quads [1..R-2] x [1..R-2]
    for (uint32_t iz = 1u; iz < R - 1u; ++iz) {
        for (uint32_t ix = 1u; ix < R - 1u; ++ix) {
            emitQuad(ix, iz);
        }
    }

    // 2. North border strip (iz == 0)
    if (northCoarse) {
        for (uint32_t ix = 0u; ix < R; ix += 2u) {
            const uint32_t t0 = vid(ix, 0u);
            const uint32_t t2 = vid(ix + 2u, 0u);
            const uint32_t b0 = vid(ix, 1u);
            const uint32_t b1 = vid(ix + 1u, 1u);
            const uint32_t b2 = vid(ix + 2u, 1u);

            if (!(ix == 0u && westCoarse)) {
                indices.push_back(t0);
                indices.push_back(b0);
                indices.push_back(b1);
            }
            indices.push_back(t0);
            indices.push_back(b1);
            indices.push_back(t2);
            if (!(ix + 2u == R && eastCoarse)) {
                indices.push_back(t2);
                indices.push_back(b1);
                indices.push_back(b2);
            }
        }
    } else {
        const uint32_t startX = westCoarse ? 1u : 0u;
        const uint32_t endX   = eastCoarse ? (R - 1u) : R;
        for (uint32_t ix = startX; ix < endX; ++ix) {
            emitQuad(ix, 0u);
        }
    }

    // 3. South border strip (iz == R - 1)
    if (southCoarse) {
        for (uint32_t ix = 0u; ix < R; ix += 2u) {
            const uint32_t t0 = vid(ix, R - 1u);
            const uint32_t t1 = vid(ix + 1u, R - 1u);
            const uint32_t t2 = vid(ix + 2u, R - 1u);
            const uint32_t b0 = vid(ix, R);
            const uint32_t b2 = vid(ix + 2u, R);

            if (!(ix == 0u && westCoarse)) {
                indices.push_back(t0);
                indices.push_back(b0);
                indices.push_back(t1);
            }
            indices.push_back(t1);
            indices.push_back(b0);
            indices.push_back(b2);
            if (!(ix + 2u == R && eastCoarse)) {
                indices.push_back(t1);
                indices.push_back(b2);
                indices.push_back(t2);
            }
        }
    } else {
        const uint32_t startX = westCoarse ? 1u : 0u;
        const uint32_t endX   = eastCoarse ? (R - 1u) : R;
        for (uint32_t ix = startX; ix < endX; ++ix) {
            emitQuad(ix, R - 1u);
        }
    }

    // 4. West border strip (ix == 0)
    if (westCoarse) {
        for (uint32_t iz = 0u; iz < R; iz += 2u) {
            const uint32_t l0 = vid(0u, iz);
            const uint32_t l2 = vid(0u, iz + 2u);
            const uint32_t r0 = vid(1u, iz);
            const uint32_t r1 = vid(1u, iz + 1u);
            const uint32_t r2 = vid(1u, iz + 2u);

            if (!(iz == 0u && northCoarse)) {
                indices.push_back(l0);
                indices.push_back(r1);
                indices.push_back(r0);
            }
            indices.push_back(l0);
            indices.push_back(l2);
            indices.push_back(r1);
            if (!(iz + 2u == R && southCoarse)) {
                indices.push_back(l2);
                indices.push_back(r2);
                indices.push_back(r1);
            }
        }
    } else {
        // Corners (iz == 0 and iz == R - 1) were already emitted by North/South strips
        for (uint32_t iz = 1u; iz < R - 1u; ++iz) {
            emitQuad(0u, iz);
        }
    }

    // 5. East border strip (ix == R - 1)
    if (eastCoarse) {
        for (uint32_t iz = 0u; iz < R; iz += 2u) {
            const uint32_t l0 = vid(R - 1u, iz);
            const uint32_t l1 = vid(R - 1u, iz + 1u);
            const uint32_t l2 = vid(R - 1u, iz + 2u);
            const uint32_t r0 = vid(R, iz);
            const uint32_t r2 = vid(R, iz + 2u);

            if (!(iz == 0u && northCoarse)) {
                indices.push_back(l0);
                indices.push_back(l1);
                indices.push_back(r0);
            }
            indices.push_back(r0);
            indices.push_back(l1);
            indices.push_back(r2);
            if (!(iz + 2u == R && southCoarse)) {
                indices.push_back(l1);
                indices.push_back(l2);
                indices.push_back(r2);
            }
        }
    } else {
        // Corners (iz == 0 and iz == R - 1) were already emitted by North/South strips
        for (uint32_t iz = 1u; iz < R - 1u; ++iz) {
            emitQuad(R - 1u, iz);
        }
    }

    return indices;
}

void TerrainStitchIndexPool::init(uint32_t patchResolution,
                                  const VulkanDevice* device,
                                  VkCommandPool transferCommandPool) {
    destroy();
    m_patchResolution = (patchResolution < 4u) ? 4u : ((patchResolution & 1u) ? patchResolution + 1u : patchResolution);
    m_gpuMemoryBytes = 0;

    for (uint8_t mask = 0; mask < 16; ++mask) {
        m_cpuIndices[mask] = generateStitchIndices(m_patchResolution, mask);
        if (device && transferCommandPool != VK_NULL_HANDLE && !m_cpuIndices[mask].empty()) {
            const VkDeviceSize size = static_cast<VkDeviceSize>(m_cpuIndices[mask].size() * sizeof(uint32_t));
            m_gpuBuffers[mask] = VulkanBuffer::createDeviceLocalBuffer(
                *device,
                transferCommandPool,
                m_cpuIndices[mask].data(),
                size,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT
            );
            if (m_gpuBuffers[mask]) {
                m_gpuMemoryBytes += static_cast<uint64_t>(m_gpuBuffers[mask]->getSize());
            }
        }
    }
}

void TerrainStitchIndexPool::destroy() noexcept {
    for (auto& buf : m_gpuBuffers) {
        buf.reset();
    }
    for (auto& vec : m_cpuIndices) {
        vec.clear();
    }
    m_gpuMemoryBytes = 0;
}

void TerrainPatchMesh::generate(uint32_t patchResolution,
                                float uMin, float uMax,
                                float vMin, float vMax,
                                uint32_t lodLevel,
                                const TerrainConfig& config,
                                const HeightmapData& heightmap,
                                const VulkanDevice* device,
                                VkCommandPool transferCommandPool) {
    const uint32_t R = (patchResolution < 4u) ? 4u : ((patchResolution & 1u) ? patchResolution + 1u : patchResolution);
    const uint32_t stride = R + 1u;
    const float invR = 1.0f / static_cast<float>(R);

    m_cpuVertices.clear();
    m_cpuVertices.resize(static_cast<size_t>(stride) * stride);

    float minX = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();

    // Pass 1: Sample global heightmap for exact world position, normal, and UV
    for (uint32_t iz = 0u; iz <= R; ++iz) {
        const float tz = static_cast<float>(iz) * invR;
        const float v = vMin + (vMax - vMin) * tz;
        const float worldZ = (v - 0.5f) * config.length;

        for (uint32_t ix = 0u; ix <= R; ++ix) {
            const float tx = static_cast<float>(ix) * invR;
            const float u = uMin + (uMax - uMin) * tx;
            const float worldX = (u - 0.5f) * config.width;

            const float normH = heightmap.sampleBilinear(u, v);
            const float worldY = config.heightOffset + normH * config.heightScale;

            const glm::vec3 normal = heightmap.computeNormal(u, v, config.width, config.length, config.heightScale);
            const glm::vec2 uv(u * config.textureTiling, v * config.textureTiling);

            Vertex& vert = m_cpuVertices[static_cast<size_t>(iz) * stride + ix];
            vert.position = glm::vec3(worldX, worldY, worldZ);
            vert.color = glm::vec3(worldY, static_cast<float>(lodLevel), 0.0f);
            vert.uv = uv;
            vert.normal = normal;

            minX = std::min(minX, worldX);
            maxX = std::max(maxX, worldX);
            minY = std::min(minY, worldY);
            maxY = std::max(maxY, worldY);
            minZ = std::min(minZ, worldZ);
            maxZ = std::max(maxZ, worldZ);
        }
    }

    // Pass 2: Compute exact parent-surface height for GPU geomorphing and world-space geometric error.
    // Parent grid has vertices at even (ix, iz) and triangulates each 2x2 cell along diagonal (ix+2, iz)-(ix, iz+2).
    float maxGeomError = 0.0f;
    for (uint32_t iz = 0u; iz <= R; ++iz) {
        for (uint32_t ix = 0u; ix <= R; ++ix) {
            Vertex& vert = m_cpuVertices[static_cast<size_t>(iz) * stride + ix];
            const bool oddX = (ix & 1u) != 0u;
            const bool oddZ = (iz & 1u) != 0u;

            float parentY = vert.position.y;
            float morphMask = 0.0f;
            if (oddX && !oddZ) {
                const float hL = m_cpuVertices[static_cast<size_t>(iz) * stride + (ix - 1u)].position.y;
                const float hR = m_cpuVertices[static_cast<size_t>(iz) * stride + (ix + 1u)].position.y;
                parentY = 0.5f * (hL + hR);
                morphMask = 1.0f;
            } else if (!oddX && oddZ) {
                const float hD = m_cpuVertices[static_cast<size_t>(iz - 1u) * stride + ix].position.y;
                const float hU = m_cpuVertices[static_cast<size_t>(iz + 1u) * stride + ix].position.y;
                parentY = 0.5f * (hD + hU);
                morphMask = 1.0f;
            } else if (oddX && oddZ) {
                // Matches parent quad diagonal v10(ix+1, iz-1) -- v01(ix-1, iz+1)
                const float hTR = m_cpuVertices[static_cast<size_t>(iz - 1u) * stride + (ix + 1u)].position.y;
                const float hBL = m_cpuVertices[static_cast<size_t>(iz + 1u) * stride + (ix - 1u)].position.y;
                parentY = 0.5f * (hTR + hBL);
                morphMask = 1.0f;
            }

            // Encode vertex role in color.z for neighbor-synchronized GPU geomorphing:
            // 0 = even (no morph), 1 = interior odd, 2 = North odd, 3 = South odd, 4 = West odd, 5 = East odd
            if (morphMask > 0.5f) {
                if (iz == 0u)      morphMask = 2.0f; // North edge odd
                else if (iz == R)  morphMask = 3.0f; // South edge odd
                else if (ix == 0u) morphMask = 4.0f; // West edge odd
                else if (ix == R)  morphMask = 5.0f; // East edge odd
                else               morphMask = 1.0f; // Interior odd
            }

            vert.color.x = parentY;
            vert.color.y = static_cast<float>(lodLevel);
            vert.color.z = morphMask;

            maxGeomError = std::max(maxGeomError, std::abs(vert.position.y - parentY));
        }
    }

    m_minHeight = minY;
    m_maxHeight = maxY;
    m_bounds.min = glm::vec3(minX, minY, minZ);
    m_bounds.max = glm::vec3(maxX, maxY, maxZ);
    m_geometricError = maxGeomError;

    if (device && transferCommandPool != VK_NULL_HANDLE) {
        uploadToGpu(*device, transferCommandPool);
    }
}

bool TerrainPatchMesh::uploadToGpu(const VulkanDevice& device, VkCommandPool transferCommandPool) {
    if (m_cpuVertices.empty()) {
        return false;
    }
    const VkDeviceSize size = static_cast<VkDeviceSize>(m_cpuVertices.size() * sizeof(Vertex));
    m_vertexBuffer = VulkanBuffer::createDeviceLocalBuffer(
        device,
        transferCommandPool,
        m_cpuVertices.data(),
        size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    );
    return m_vertexBuffer != nullptr;
}

void TerrainPatchMesh::releaseGpu() noexcept {
    m_vertexBuffer.reset();
}

void TerrainPatchMesh::bindAndDraw(VkCommandBuffer cmd,
                                   const TerrainStitchIndexPool& indexPool,
                                   uint8_t stitchMask) const noexcept {
    if (!m_vertexBuffer) {
        return;
    }
    const VulkanBuffer* idxBuf = indexPool.getIndexBuffer(stitchMask);
    const uint32_t idxCount = indexPool.getIndexCount(stitchMask);
    if (!idxBuf || idxCount == 0u) {
        return;
    }

    const VkBuffer vb = m_vertexBuffer->getHandle();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
    vkCmdBindIndexBuffer(cmd, idxBuf->getHandle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, idxCount, 1, 0, 0, 0);
}

} // namespace sidera
