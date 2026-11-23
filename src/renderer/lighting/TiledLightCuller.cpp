#include "renderer/lighting/TiledLightCuller.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanShader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

namespace sidera {

namespace {
struct OverflowStatsGPU {
    uint32_t overflowCount = 0;
    uint32_t totalAssigned = 0;
    uint32_t maxLightsInTile = 0;
    uint32_t pad0 = 0;
};
} // namespace

TiledLightCuller::~TiledLightCuller() {
    destroy();
}

bool TiledLightCuller::init(uint32_t screenWidth, uint32_t screenHeight, const VulkanDevice* device) {
    destroy();
    m_device = device;
    if (m_device) {
        if (!createComputePipeline()) {
            SIDERA_LOG_WARN("TiledLightCuller", "light_culling.comp.spv unavailable; running CPU fallback.");
        }
    }
    return resize(screenWidth, screenHeight);
}

bool TiledLightCuller::resize(uint32_t screenWidth, uint32_t screenHeight) {
    m_width = std::max(screenWidth, 16u);
    m_height = std::max(screenHeight, 16u);
    m_tilesX = (m_width + kTileSize - 1u) / kTileSize;
    m_tilesY = (m_height + kTileSize - 1u) / kTileSize;

    m_stats.tileSize = kTileSize;
    m_stats.tilesX = m_tilesX;
    m_stats.tilesY = m_tilesY;
    m_stats.totalTiles = m_tilesX * m_tilesY;

    if (m_device && m_computePipeline != VK_NULL_HANDLE) {
        allocateBuffers();
        updateDescriptorSets();
    }

    m_initialized = true;
    SIDERA_LOG_INFO("TiledLightCuller", "Initialized 16x16 tiled light culler ({}x{} -> {}x{} tiles, gpuActive={}).",
                    m_width, m_height, m_tilesX, m_tilesY, isGpuActive() ? "YES" : "NO");
    return true;
}

void TiledLightCuller::destroy() noexcept {
    if (m_device && m_device->getLogicalDevice() != VK_NULL_HANDLE) {
        const VkDevice dev = m_device->getLogicalDevice();
        if (m_computePipeline != VK_NULL_HANDLE) { vkDestroyPipeline(dev, m_computePipeline, nullptr); m_computePipeline = VK_NULL_HANDLE; }
        if (m_pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
        if (m_descriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
        if (m_descriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(dev, m_descriptorSetLayout, nullptr); m_descriptorSetLayout = VK_NULL_HANDLE; }
    }
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_lightBuffers[i].reset();
        m_tileIndexBuffers[i].reset();
        m_tileCountBuffers[i].reset();
        m_overflowBuffers[i].reset();
    }
    m_initialized = false;
}

bool TiledLightCuller::createComputePipeline() {
    const VkDevice dev = m_device->getLogicalDevice();
    const std::vector<std::string> searchPaths = {
#ifdef SIDERA_SHADER_BINARY_DIR
        SIDERA_SHADER_BINARY_DIR,
#endif
        "shaders",
        "../shaders"
    };
    std::string spvPath;
    for (const auto& dir : searchPaths) {
        const auto p = std::filesystem::path(dir) / "light_culling.comp.spv";
        if (std::filesystem::exists(p)) {
            spvPath = p.string();
            break;
        }
    }
    if (spvPath.empty()) return false;

    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    for (uint32_t i = 0; i < 4u; ++i) {
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }

    VkDescriptorSetLayoutCreateInfo dslInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 4, bindings.data()};
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &dslInfo, nullptr, &m_descriptorSetLayout), "Failed to create light cull DSL");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4u * kMaxFramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, kMaxFramesInFlight, 1, &poolSize};
    VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_descriptorPool), "Failed to create light cull pool");

    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> layouts{};
    layouts.fill(m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_descriptorPool, kMaxFramesInFlight, layouts.data()};
    VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, m_descriptorSets.data()), "Failed to allocate light cull descriptor sets");

    VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(LightCullPC)};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &m_descriptorSetLayout, 1, &pcRange};
    VK_CHECK(vkCreatePipelineLayout(dev, &plInfo, nullptr, &m_pipelineLayout), "Failed to create light cull pipeline layout");

    VulkanShader shader(dev, spvPath);
    VkComputePipelineCreateInfo cpInfo{};
    cpInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shader.getHandle(), "main", nullptr};
    cpInfo.layout = m_pipelineLayout;
    VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_computePipeline), "Failed to create light cull compute pipeline");
    return true;
}

void TiledLightCuller::allocateBuffers() {
    const uint32_t totalTiles = std::max(m_tilesX * m_tilesY, 1u);
    const VkDeviceSize lightBytes = static_cast<VkDeviceSize>(LightManager::kMaxLights) * sizeof(GPULight);
    const VkDeviceSize idxBytes   = static_cast<VkDeviceSize>(totalTiles) * kMaxLightsPerTile * sizeof(uint32_t);
    const VkDeviceSize cntBytes   = static_cast<VkDeviceSize>(totalTiles) * sizeof(uint32_t);
    const VkDeviceSize ovfBytes   = sizeof(OverflowStatsGPU);

    uint64_t totalMem = 0;
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_lightBuffers[i] = std::make_unique<VulkanBuffer>(
            *m_device, lightBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_tileIndexBuffers[i] = std::make_unique<VulkanBuffer>(
            *m_device, idxBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        m_tileCountBuffers[i] = std::make_unique<VulkanBuffer>(
            *m_device, cntBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        m_overflowBuffers[i] = std::make_unique<VulkanBuffer>(
            *m_device, ovfBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        totalMem += static_cast<uint64_t>(lightBytes + idxBytes + cntBytes + ovfBytes);
    }
    m_stats.bufferMemoryBytes = totalMem;
}

void TiledLightCuller::updateDescriptorSets() {
    const VkDevice dev = m_device->getLogicalDevice();
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        std::array<VkDescriptorBufferInfo, 4> infos{};
        infos[0] = {m_lightBuffers[i]->getHandle(), 0, m_lightBuffers[i]->getSize()};
        infos[1] = {m_tileIndexBuffers[i]->getHandle(), 0, m_tileIndexBuffers[i]->getSize()};
        infos[2] = {m_tileCountBuffers[i]->getHandle(), 0, m_tileCountBuffers[i]->getSize()};
        infos[3] = {m_overflowBuffers[i]->getHandle(), 0, m_overflowBuffers[i]->getSize()};

        std::array<VkWriteDescriptorSet, 4> writes{};
        for (uint32_t b = 0; b < 4u; ++b) {
            writes[b] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSets[i], b, 0, 1,
                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &infos[b], nullptr};
        }
        vkUpdateDescriptorSets(dev, 4, writes.data(), 0, nullptr);
    }
}

void TiledLightCuller::uploadLights(uint32_t frameIndex, const LightManager& lightManager) {
    const uint32_t slot = frameIndex % kMaxFramesInFlight;
    if (!m_lightBuffers[slot]) return;
    const auto& lights = lightManager.getLights();
    if (!lights.empty()) {
        m_lightBuffers[slot]->uploadData(lights.data(), static_cast<VkDeviceSize>(lights.size()) * sizeof(GPULight), 0);
    }
    m_stats.totalLights = static_cast<uint32_t>(lights.size());
}

void TiledLightCuller::recordCull(VkCommandBuffer cmd, uint32_t frameIndex, const Camera& camera, uint32_t lightCount) {
    if (!isGpuActive()) return;
    const uint32_t slot = frameIndex % kMaxFramesInFlight;

    OverflowStatsGPU zeroOvf{};
    m_overflowBuffers[slot]->uploadData(&zeroOvf, sizeof(OverflowStatsGPU), 0);

    const glm::mat4& proj = camera.getProjectionMatrix();
    LightCullPC pc{};
    pc.view = camera.getViewMatrix();
    pc.projParams = glm::vec4(proj[0][0], proj[1][1], camera.getNearPlane(), camera.getFarPlane());
    pc.tileParams = glm::uvec4(m_width, m_height, m_tilesX, lightCount);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSets[slot], 0, nullptr);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(LightCullPC), &pc);
    vkCmdDispatch(cmd, m_tilesX, m_tilesY, 1u);

    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 1, &barrier, 0, nullptr, 0, nullptr};
    vkCmdPipelineBarrier2(cmd, &dep);
}

TiledLightStats TiledLightCuller::simulateCullCPU(const Camera& camera,
                                                  const LightManager& lightManager,
                                                  std::vector<uint32_t>& outTileCounts) const {
    const auto t0 = std::chrono::high_resolution_clock::now();
    TiledLightStats st = m_stats;
    const auto& lights = lightManager.getLights();
    st.totalLights = static_cast<uint32_t>(lights.size());
    st.maxLightsInTile = 0;
    st.overflowCount = 0;

    outTileCounts.assign(static_cast<size_t>(m_tilesX) * m_tilesY, 0u);
    const glm::mat4 view = camera.getViewMatrix();
    const glm::mat4 proj = camera.getProjectionMatrix();
    const float nearP = camera.getNearPlane();
    const float farP  = camera.getFarPlane();

    uint64_t totalAssigned = 0;
    for (uint32_t ty = 0; ty < m_tilesY; ++ty) {
        const float minY = (static_cast<float>(ty * kTileSize) / static_cast<float>(m_height)) * 2.0f - 1.0f;
        const float maxY = (static_cast<float>((ty + 1u) * kTileSize) / static_cast<float>(m_height)) * 2.0f - 1.0f;
        for (uint32_t tx = 0; tx < m_tilesX; ++tx) {
            const float minX = (static_cast<float>(tx * kTileSize) / static_cast<float>(m_width)) * 2.0f - 1.0f;
            const float maxX = (static_cast<float>((tx + 1u) * kTileSize) / static_cast<float>(m_width)) * 2.0f - 1.0f;

            uint32_t count = 0;
            for (const auto& L : lights) {
                if (L.spotParams.z < 0.5f) continue;
                const uint32_t lType = static_cast<uint32_t>(L.directionType.w + 0.5f);
                bool hit = (lType == 0u);
                if (!hit) {
                    const glm::vec3 vp = glm::vec3(view * glm::vec4(glm::vec3(L.positionRadius), 1.0f));
                    const float r = std::max(L.positionRadius.w, 0.1f);
                    const float depth = -vp.z;
                    if (depth + r >= nearP && depth - r <= farP) {
                        const float safeZ = std::max(depth, nearP);
                        const float ndcX = (vp.x * proj[0][0]) / safeZ;
                        const float ndcY = (vp.y * proj[1][1]) / safeZ;
                        const float radX = (r * std::abs(proj[0][0])) / safeZ;
                        const float radY = (r * std::abs(proj[1][1])) / safeZ;
                        if (ndcX + radX >= minX && ndcX - radX <= maxX &&
                            ndcY + radY >= minY && ndcY - radY <= maxY) {
                            hit = true;
                        }
                    }
                }
                if (hit) {
                    if (count < kMaxLightsPerTile) {
                        ++count;
                    } else {
                        ++st.overflowCount;
                    }
                }
            }
            outTileCounts[static_cast<size_t>(ty) * m_tilesX + tx] = count;
            st.maxLightsInTile = std::max(st.maxLightsInTile, count);
            totalAssigned += count;
        }
    }

    st.averageLightsPerTile = (st.totalTiles > 0u) ? (static_cast<float>(totalAssigned) / static_cast<float>(st.totalTiles)) : 0.0f;
    const auto t1 = std::chrono::high_resolution_clock::now();
    st.cullTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    return st;
}

} // namespace sidera
