#include "terrain/Terrain.h"
#include "core/Log.h"
#include "renderer/Renderer.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <glm/geometric.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace sidera {

namespace {

struct TerrainPushConstants {
    glm::mat4 model{1.0f};
    glm::vec4 terrainParams{120.0f, 120.0f, 14.0f, 24.0f}; // x = width, y = length, z = heightScale, w = textureTiling
    glm::vec4 viewportParams{0.0f, 0.0f, 0.1f, 100.0f};     // x = mode, y = normalSpace, z = near, w = far
};
static_assert(sizeof(TerrainPushConstants) == 96, "TerrainPushConstants must be 96 bytes.");

} // namespace

Terrain::Terrain() = default;

Terrain::~Terrain() {
    destroy();
}

bool Terrain::init(const TerrainConfig& config,
                   const VulkanDevice& device,
                   VkCommandPool transferCommandPool,
                   TextureManager& textureManager) {
    const auto startTime = std::chrono::high_resolution_clock::now();

    m_device = &device;
    m_transferCommandPool = transferCommandPool;
    m_textureManager = &textureManager;
    m_config = config;

    SIDERA_LOG_INFO("Terrain", "Initializing Terrain System ({}x{}m, chunks: {}x{}, chunkRes: {} quads)...",
                    m_config.width, m_config.length, m_config.chunksX, m_config.chunksZ, m_config.chunkResolution);

    // 1. Load or procedurally generate heightmap
    if (!m_config.heightmapPath.empty() && m_heightmap.loadFromFile(m_config.heightmapPath)) {
        SIDERA_LOG_INFO("Terrain", "Loaded heightmap from '{}'.", m_config.heightmapPath);
    } else {
        SIDERA_LOG_INFO("Terrain", "Generating procedural multi-octave heightmap (seed {})...", m_config.proceduralSeed);
        m_heightmap.generateProcedural(513, m_config.proceduralSeed, m_config.proceduralOctaves, m_config.proceduralFrequency);
    }

    // 2. Initialize multi-layer PBR materials and splat map
    m_material = std::make_unique<TerrainMaterial>(*m_device, m_transferCommandPool, *m_textureManager);
    m_material->init(m_config, m_heightmap);

    // 3. Build terrain chunks
    buildChunks();

    // 4. Calculate terrain aggregate bounding box
    computeBounds();

    const auto endTime = std::chrono::high_resolution_clock::now();
    const float durationMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    m_stats.generationTimeMs = durationMs;
    m_stats.heightmapResolution = m_heightmap.getWidth();

    SIDERA_LOG_INFO("Terrain", "Terrain system initialized successfully in {:.2f} ms: {} chunks, {} vertices, {} triangles.",
                    durationMs, m_stats.totalChunks, m_stats.totalVertices, m_stats.totalTriangles);

    m_initialized = true;
    return true;
}

bool Terrain::initHeadless(const TerrainConfig& config) {
    m_config = config;
    if (!m_config.heightmapPath.empty() && m_heightmap.loadFromFile(m_config.heightmapPath)) {
        SIDERA_LOG_INFO("Terrain", "Loaded heightmap from '{}'.", m_config.heightmapPath);
    } else {
        m_heightmap.generateProcedural(513, m_config.proceduralSeed, m_config.proceduralOctaves, m_config.proceduralFrequency);
    }

    const float halfW = m_config.width * 0.5f;
    const float halfL = m_config.length * 0.5f;
    m_bounds.aabb.min = glm::vec3(-halfW, m_config.heightOffset, -halfL);
    m_bounds.aabb.max = glm::vec3(halfW, m_config.heightOffset + m_config.heightScale, halfL);
    m_bounds.center = (m_bounds.aabb.min + m_bounds.aabb.max) * 0.5f;
    m_bounds.size = glm::vec2(m_config.width, m_config.length);
    m_bounds.minHeight = m_bounds.aabb.min.y;
    m_bounds.maxHeight = m_bounds.aabb.max.y;

    m_initialized = true;
    return true;
}

void Terrain::rebuild(const TerrainConfig& newConfig) {
    if (!m_device || !m_textureManager) {
        return;
    }
    const VulkanDevice* dev = m_device;
    const VkCommandPool pool = m_transferCommandPool;
    TextureManager* texMgr = m_textureManager;

    destroy();
    init(newConfig, *dev, pool, *texMgr);
}

void Terrain::destroy() noexcept {
    m_chunks.clear();
    m_material.reset();
    m_initialized = false;
}

void Terrain::buildChunks() {
    m_chunks.clear();
    m_chunks.reserve(static_cast<size_t>(m_config.chunksX) * m_config.chunksZ);

    uint64_t totalVerts = 0;
    uint64_t totalTris = 0;
    uint64_t totalGpuMem = 0;

    uint32_t chunkId = 0;
    for (uint32_t cz = 0; cz < m_config.chunksZ; ++cz) {
        for (uint32_t cx = 0; cx < m_config.chunksX; ++cx) {
            auto chunk = std::make_unique<TerrainChunk>(
                chunkId++,
                cx,
                cz,
                m_config,
                m_heightmap,
                *m_device,
                m_transferCommandPool
            );

            totalVerts += chunk->getVertexCount();
            totalTris += chunk->getTriangleCount();
            totalGpuMem += static_cast<uint64_t>(chunk->getVertexCount()) * sizeof(Vertex) +
                           static_cast<uint64_t>(chunk->getTriangleCount() * 3) * sizeof(uint32_t);

            m_chunks.push_back(std::move(chunk));
        }
    }

    m_stats.totalChunks = static_cast<uint32_t>(m_chunks.size());
    m_stats.visibleChunks = m_stats.totalChunks;
    m_stats.culledChunks = 0;
    m_stats.totalVertices = totalVerts;
    m_stats.totalTriangles = totalTris;
    m_stats.visibleTriangles = totalTris;
    m_stats.gpuMemoryUsageBytes = totalGpuMem;
}

void Terrain::recalculateBounds() noexcept {
    computeBounds();
}

void Terrain::addChunk(std::unique_ptr<TerrainChunk> chunk) {
    if (!chunk) return;
    m_chunks.push_back(std::move(chunk));
    m_stats.totalChunks = static_cast<uint32_t>(m_chunks.size());
    recalculateBounds();
}

bool Terrain::removeChunk(uint32_t chunkId) {
    auto it = std::find_if(m_chunks.begin(), m_chunks.end(), [chunkId](const std::unique_ptr<TerrainChunk>& c) {
        return c && c->getChunkId() == chunkId;
    });
    if (it != m_chunks.end()) {
        m_chunks.erase(it);
        m_stats.totalChunks = static_cast<uint32_t>(m_chunks.size());
        recalculateBounds();
        return true;
    }
    return false;
}

void Terrain::clearChunks() noexcept {
    m_chunks.clear();
    m_stats.totalChunks = 0;
    m_stats.visibleChunks = 0;
    m_stats.culledChunks = 0;
    m_stats.visibleTriangles = 0;
    m_stats.totalVertices = 0;
    m_stats.totalTriangles = 0;
}

TerrainChunk* Terrain::getChunk(uint32_t chunkId) noexcept {
    for (auto& chunk : m_chunks) {
        if (chunk && chunk->getChunkId() == chunkId) {
            return chunk.get();
        }
    }
    return nullptr;
}

const TerrainChunk* Terrain::getChunk(uint32_t chunkId) const noexcept {
    for (const auto& chunk : m_chunks) {
        if (chunk && chunk->getChunkId() == chunkId) {
            return chunk.get();
        }
    }
    return nullptr;
}

void Terrain::computeBounds() {
    float minX = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();

    for (const auto& chunk : m_chunks) {
        const AABB& b = chunk->getBoundingBox();
        minX = std::min(minX, b.min.x);
        maxX = std::max(maxX, b.max.x);
        minY = std::min(minY, b.min.y);
        maxY = std::max(maxY, b.max.y);
        minZ = std::min(minZ, b.min.z);
        maxZ = std::max(maxZ, b.max.z);
    }

    m_bounds.aabb.min = glm::vec3(minX, minY, minZ);
    m_bounds.aabb.max = glm::vec3(maxX, maxY, maxZ);
    m_bounds.center = (m_bounds.aabb.min + m_bounds.aabb.max) * 0.5f;
    m_bounds.size = glm::vec2(maxX - minX, maxZ - minZ);
    m_bounds.minHeight = minY;
    m_bounds.maxHeight = maxY;
}

void Terrain::updateVisibility(const Frustum& frustum) noexcept {
    uint32_t visibleCount = 0;
    uint32_t culledCount = 0;
    uint64_t visibleTris = 0;

    for (const auto& chunk : m_chunks) {
        const bool visible = frustum.intersectsAABB(chunk->getBoundingBox());
        chunk->setVisible(visible);
        if (visible) {
            ++visibleCount;
            visibleTris += chunk->getTriangleCount();
        } else {
            ++culledCount;
        }
    }

    m_stats.visibleChunks = visibleCount;
    m_stats.culledChunks = culledCount;
    m_stats.visibleTriangles = visibleTris;
}

float Terrain::getHeightAt(float worldX, float worldZ, bool* outOfBounds) const noexcept {
    const float halfW = m_config.width * 0.5f;
    const float halfL = m_config.length * 0.5f;

    const bool isOutside = (worldX < -halfW || worldX > halfW || worldZ < -halfL || worldZ > halfL);
    if (outOfBounds) {
        *outOfBounds = isOutside;
    }

    const float clampedX = std::clamp(worldX, -halfW, halfW);
    const float clampedZ = std::clamp(worldZ, -halfL, halfL);

    const float u = (clampedX + halfW) / m_config.width;
    const float v = (clampedZ + halfL) / m_config.length;

    const float normH = m_heightmap.sampleBilinear(u, v);
    return m_config.heightOffset + normH * m_config.heightScale;
}

glm::vec3 Terrain::getNormalAt(float worldX, float worldZ) const noexcept {
    const float halfW = m_config.width * 0.5f;
    const float halfL = m_config.length * 0.5f;

    const float u = std::clamp((worldX + halfW) / m_config.width, 0.0f, 1.0f);
    const float v = std::clamp((worldZ + halfL) / m_config.length, 0.0f, 1.0f);

    return m_heightmap.computeNormal(u, v, m_config.width, m_config.length, m_config.heightScale);
}

bool Terrain::containsWorldPoint(float worldX, float worldZ) const noexcept {
    const float halfW = m_config.width * 0.5f;
    const float halfL = m_config.length * 0.5f;
    return worldX >= -halfW && worldX <= halfW && worldZ >= -halfL && worldZ <= halfL;
}

uint32_t Terrain::render(VkCommandBuffer cmd, VkPipelineLayout pipelineLayout, const glm::vec4& vpParams) const noexcept {
    if (!m_initialized) {
        return 0;
    }

    const TerrainPushConstants pc{
        .model = glm::mat4(1.0f),
        .terrainParams = glm::vec4(m_config.width, m_config.length, m_config.heightScale, m_config.textureTiling),
        .viewportParams = vpParams
    };

    vkCmdPushConstants(
        cmd,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(TerrainPushConstants),
        &pc
    );

    uint32_t drawCount = 0;
    for (const auto& chunk : m_chunks) {
        if (!chunk || !chunk->isVisible() || !chunk->isGpuResident()) {
            continue;
        }

        chunk->bind(cmd);
        chunk->draw(cmd);
        ++drawCount;
    }

    m_stats.mainDrawCalls = drawCount;
    return drawCount;
}

uint32_t Terrain::renderShadow(VkCommandBuffer cmd, VkPipelineLayout shadowLayout, const glm::mat4& lightSpaceMatrix) const noexcept {
    if (!m_initialized) {
        return 0;
    }

    uint32_t drawCount = 0;
    for (const auto& chunk : m_chunks) {
        // Draw chunks within reasonable shadow caster range
        if (!chunk || !chunk->isVisible() || !chunk->isGpuResident()) {
            continue;
        }

        const ShadowPushConstants shadowPc{
            .lightSpaceMatrix = lightSpaceMatrix,
            .model = chunk->getWorldTransform()
        };

        vkCmdPushConstants(
            cmd,
            shadowLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(ShadowPushConstants),
            &shadowPc
        );

        chunk->bind(cmd);
        chunk->draw(cmd);
        ++drawCount;
    }

    m_stats.shadowDrawCalls = drawCount;
    return drawCount;
}

} // namespace sidera
