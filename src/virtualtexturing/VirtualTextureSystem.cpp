#include "virtualtexturing/VirtualTextureSystem.h"
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
struct GPUFeedbackHeader {
    uint32_t requestCount = 0;
    uint32_t overflowCount = 0;
    uint32_t hitCount = 0;
    uint32_t missCount = 0;
};
} // namespace

VirtualTextureSystem::~VirtualTextureSystem() {
    destroy();
}

bool VirtualTextureSystem::init(const VirtualTextureConfig& config,
                                const VulkanDevice* device,
                                VkCommandPool transferCommandPool) {
    destroy();
    m_config = config;
    m_device = device;
    m_transferCommandPool = transferCommandPool;

    m_pageTable.init(16384u);
    m_physicalCache.init(m_config.physicalCacheCapacity, m_config.getPhysicalTileSize());

    if (m_device) {
        if (createFeedbackPipeline()) {
            allocateGpuBuffers();
            updateDescriptorSets();
        } else {
            SIDERA_LOG_WARN("VirtualTexturing", "vt_feedback_resolve.comp.spv unavailable; running CPU-managed mode.");
        }
    }

    m_frameIndex = 0;
    m_initialized = true;
    updateStatistics();
    SIDERA_LOG_INFO("VirtualTexturing", "VirtualTextureSystem initialized (cache={} pages, tile={}px + {}px gutter, gpuActive={}).",
                    m_config.physicalCacheCapacity, m_config.pageSize, m_config.gutterSize, isGpuActive() ? "YES" : "NO");
    return true;
}

void VirtualTextureSystem::destroy() noexcept {
    if (m_device && m_device->getLogicalDevice() != VK_NULL_HANDLE) {
        const VkDevice dev = m_device->getLogicalDevice();
        if (m_feedbackPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(dev, m_feedbackPipeline, nullptr);
            m_feedbackPipeline = VK_NULL_HANDLE;
        }
        if (m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
            m_pipelineLayout = VK_NULL_HANDLE;
        }
        if (m_descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr);
            m_descriptorPool = VK_NULL_HANDLE;
        }
        if (m_descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(dev, m_descriptorSetLayout, nullptr);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
    }
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_pageTableBuffers[i].reset();
        m_feedbackBuffers[i].reset();
    }
    m_textures.clear();
    m_pageTable.clear();
    m_physicalCache.clear();
    m_requestQueue.clear();
    m_pendingSet.clear();
    m_initialized = false;
}

uint16_t VirtualTextureSystem::createVirtualTexture(const std::string& name,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    VirtualTextureSemantic semantic) {
    VirtualTextureAsset asset{};
    asset.textureId = static_cast<uint16_t>(m_textures.size());
    asset.name = name;
    asset.semantic = semantic;
    asset.width = std::max(width, m_config.pageSize);
    asset.height = std::max(height, m_config.pageSize);
    asset.pageSize = m_config.pageSize;
    asset.gutterSize = m_config.gutterSize;

    uint32_t mips = 1;
    uint32_t dim = std::max(asset.width, asset.height);
    while (dim > asset.pageSize && mips < 14u) {
        dim >>= 1u;
        ++mips;
    }
    asset.mipLevels = mips;
    m_textures.push_back(asset);

    // Pin coarsest fallback mip levels so sampling always has a guaranteed resident fallback
    const uint32_t pinStart = (asset.mipLevels > m_config.pinCoarsestMipLevels)
        ? (asset.mipLevels - m_config.pinCoarsestMipLevels) : 0u;
    for (uint32_t mip = pinStart; mip < asset.mipLevels; ++mip) {
        const uint32_t pxCount = asset.getPagesX(mip);
        const uint32_t pyCount = asset.getPagesY(mip);
        for (uint32_t py = 0; py < pyCount; ++py) {
            for (uint32_t px = 0; px < pxCount; ++px) {
                VirtualTexturePageID pid{asset.textureId, static_cast<uint8_t>(mip), px, py};
                uint32_t physIdx = 0;
                uint16_t ver = 0;
                if (m_physicalCache.allocateSlot(pid, m_frameIndex, true, physIdx, ver)) {
                    m_pageTable.mapPage(pid, physIdx, ver);
                }
            }
        }
    }

    updateStatistics();
    return asset.textureId;
}

void VirtualTextureSystem::requestPage(const VirtualTexturePageID& id) {
    if (m_pageTable.isResident(id)) {
        return;
    }
    const uint64_t packed = id.pack();
    if (m_pendingSet.insert(packed).second) {
        m_requestQueue.push_back(id);
    }
}

uint32_t VirtualTextureSystem::processPendingUploads() {
    const auto t0 = std::chrono::high_resolution_clock::now();
    uint32_t uploaded = 0;
    uint64_t uploadedBytes = 0;
    const uint64_t tileBytes = static_cast<uint64_t>(m_config.getPhysicalTileSize()) * m_config.getPhysicalTileSize() * 4ULL;

    while (!m_requestQueue.empty() &&
           uploaded < m_config.maxUploadsPerFrame &&
           uploadedBytes + tileBytes <= m_config.maxUploadBytesPerFrame) {
        const VirtualTexturePageID pid = m_requestQueue.front();
        m_requestQueue.pop_front();
        m_pendingSet.erase(pid.pack());

        if (m_pageTable.isResident(pid)) {
            continue;
        }

        uint32_t physIdx = 0;
        uint16_t ver = 0;
        VirtualTexturePageID evicted{};
        bool didEvict = false;
        if (m_physicalCache.allocateSlot(pid, m_frameIndex, false, physIdx, ver, &evicted, &didEvict)) {
            if (didEvict) {
                m_pageTable.unmapPage(evicted);
                ++m_stats.pageEvictionCount;
            }
            m_pageTable.mapPage(pid, physIdx, ver);
            ++uploaded;
            uploadedBytes += tileBytes;
            m_stats.pageUploadBytes += tileBytes;
            ++m_stats.pageUploadsTotal;
        } else {
            break;
        }
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    m_stats.pageUploadTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    updateStatistics();
    return uploaded;
}

std::vector<uint8_t> VirtualTextureSystem::generateTileData(const VirtualTexturePageID& id) const {
    const uint32_t tileSize = m_config.getPhysicalTileSize();
    const int32_t g = static_cast<int32_t>(m_config.gutterSize);
    const int32_t p = static_cast<int32_t>(m_config.pageSize);
    std::vector<uint8_t> pixels(static_cast<size_t>(tileSize) * tileSize * 4ULL, 255u);

    // Sample global continuous procedural pattern including gutter texels for zero-seam filtering
    const int32_t baseTexelX = static_cast<int32_t>(id.pageX) * p;
    const int32_t baseTexelY = static_cast<int32_t>(id.pageY) * p;

    for (uint32_t ty = 0; ty < tileSize; ++ty) {
        const int32_t gy = baseTexelY + static_cast<int32_t>(ty) - g;
        for (uint32_t tx = 0; tx < tileSize; ++tx) {
            const int32_t gx = baseTexelX + static_cast<int32_t>(tx) - g;
            const size_t idx = (static_cast<size_t>(ty) * tileSize + tx) * 4ULL;
            pixels[idx + 0] = static_cast<uint8_t>((gx * 3 + gy) & 0xFF);
            pixels[idx + 1] = static_cast<uint8_t>((gy * 3 + gx) & 0xFF);
            pixels[idx + 2] = static_cast<uint8_t>((id.mipLevel * 32u) & 0xFF);
            pixels[idx + 3] = 255u;
        }
    }
    return pixels;
}

PageTableEntry VirtualTextureSystem::samplePageTable(const VirtualTexturePageID& id, bool* outUsedFallback) {
    uint32_t maxMips = 8u;
    if (id.textureId < m_textures.size()) {
        maxMips = m_textures[id.textureId].mipLevels;
    }
    bool usedFallback = false;
    const PageTableEntry entry = m_pageTable.resolveWithFallback(id, maxMips, &usedFallback);
    if (entry.isResident()) {
        m_physicalCache.touchSlot(entry.physicalPageIndex, m_frameIndex);
        if (usedFallback) {
            ++m_stats.cacheMisses;
            ++m_stats.fallbackSampleCount;
            requestPage(id);
        } else {
            ++m_stats.cacheHits;
        }
    } else {
        ++m_stats.cacheMisses;
        requestPage(id);
    }
    if (outUsedFallback) *outUsedFallback = usedFallback;
    updateStatistics();
    return entry;
}

void VirtualTextureSystem::beginFrame() {
    ++m_frameIndex;
    m_physicalCache.flushRetiredSlots(m_frameIndex);
    const uint32_t slot = static_cast<uint32_t>(m_frameIndex % kMaxFramesInFlight);
    if (m_frameIndex >= kMaxFramesInFlight) {
        readbackDelayedFeedback(slot);
    }
    (void)processPendingUploads();
    syncPageTableToGpu(slot);
}

void VirtualTextureSystem::readbackDelayedFeedback(uint32_t frameIndex) {
    const auto& fb = m_feedbackBuffers[frameIndex % kMaxFramesInFlight];
    if (!fb) return;

    const auto t0 = std::chrono::high_resolution_clock::now();
    GPUFeedbackHeader hdr{};
    fb->downloadData(&hdr, sizeof(GPUFeedbackHeader), 0);
    m_stats.cacheHits += hdr.hitCount;
    m_stats.cacheMisses += hdr.missCount;
    m_stats.feedbackOverflowCount += hdr.overflowCount;

    const uint32_t count = std::min(hdr.requestCount, m_config.feedbackCapacity);
    if (count > 0u) {
        std::vector<VTFeedbackRequest> reqs(count);
        fb->downloadData(reqs.data(), static_cast<VkDeviceSize>(count) * sizeof(VTFeedbackRequest), sizeof(GPUFeedbackHeader));
        for (const auto& r : reqs) {
            requestPage(VirtualTexturePageID{
                static_cast<uint16_t>(r.textureId),
                static_cast<uint8_t>(r.mipLevel),
                r.pageX,
                r.pageY
            });
        }
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    m_stats.feedbackProcessTimeMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
}

void VirtualTextureSystem::syncPageTableToGpu(uint32_t frameIndex) {
    const auto& ptBuf = m_pageTableBuffers[frameIndex % kMaxFramesInFlight];
    if (!ptBuf) return;
    const auto& entries = m_pageTable.getFlatEntries();
    if (!entries.empty()) {
        const VkDeviceSize bytes = std::min<VkDeviceSize>(ptBuf->getSize(), entries.size() * sizeof(PageTableEntry));
        ptBuf->uploadData(entries.data(), bytes, 0);
    }
}

bool VirtualTextureSystem::createFeedbackPipeline() {
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
        const auto p = std::filesystem::path(dir) / "vt_feedback_resolve.comp.spv";
        if (std::filesystem::exists(p)) {
            spvPath = p.string();
            break;
        }
    }
    if (spvPath.empty()) return false;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (uint32_t i = 0; i < 2u; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 2;
    dslInfo.pBindings = bindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &dslInfo, nullptr, &m_descriptorSetLayout),
             "Failed to create VT descriptor set layout");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2u * kMaxFramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = kMaxFramesInFlight;
    VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_descriptorPool),
             "Failed to create VT descriptor pool");

    std::array<VkDescriptorSetLayout, kMaxFramesInFlight> layouts{};
    layouts.fill(m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = kMaxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, m_descriptorSets.data()),
             "Failed to allocate VT descriptor sets");

    VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VTFeedbackPC)};
    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descriptorSetLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;
    VK_CHECK(vkCreatePipelineLayout(dev, &plInfo, nullptr, &m_pipelineLayout),
             "Failed to create VT pipeline layout");

    VulkanShader shader(dev, spvPath);
    VkComputePipelineCreateInfo cpInfo{};
    cpInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpInfo.stage.module = shader.getHandle();
    cpInfo.stage.pName = "main";
    cpInfo.layout = m_pipelineLayout;
    VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_feedbackPipeline),
             "Failed to create VT feedback compute pipeline");
    return true;
}

void VirtualTextureSystem::allocateGpuBuffers() {
    const VkDeviceSize ptBytes = static_cast<VkDeviceSize>(m_pageTable.getMemoryBytes());
    const VkDeviceSize fbBytes = sizeof(GPUFeedbackHeader) + static_cast<VkDeviceSize>(m_config.feedbackCapacity) * sizeof(VTFeedbackRequest);

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        m_pageTableBuffers[i] = std::make_unique<VulkanBuffer>(
            *m_device, ptBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_feedbackBuffers[i] = std::make_unique<VulkanBuffer>(
            *m_device, fbBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        GPUFeedbackHeader zeroHdr{};
        m_feedbackBuffers[i]->uploadData(&zeroHdr, sizeof(GPUFeedbackHeader), 0);
    }
}

void VirtualTextureSystem::updateDescriptorSets() {
    const VkDevice dev = m_device->getLogicalDevice();
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkDescriptorBufferInfo ptInfo{m_pageTableBuffers[i]->getHandle(), 0, m_pageTableBuffers[i]->getSize()};
        VkDescriptorBufferInfo fbInfo{m_feedbackBuffers[i]->getHandle(), 0, m_feedbackBuffers[i]->getSize()};
        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &ptInfo, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &fbInfo, nullptr};
        vkUpdateDescriptorSets(dev, 2, writes.data(), 0, nullptr);
    }
}

void VirtualTextureSystem::recordFeedbackPass(VkCommandBuffer cmd, uint32_t frameIndex, const Camera& camera, float worldSize) {
    if (!isGpuActive() || m_textures.empty()) return;
    const uint32_t slot = frameIndex % kMaxFramesInFlight;

    GPUFeedbackHeader zeroHdr{};
    m_feedbackBuffers[slot]->uploadData(&zeroHdr, sizeof(GPUFeedbackHeader), 0);

    const auto& tex = m_textures[0];
    VTFeedbackPC pc{};
    pc.cameraPosFootprint = glm::vec4(camera.getPosition(), worldSize);
    pc.gridParams = glm::uvec4(256u, tex.textureId, tex.getPagesX(0), m_config.feedbackCapacity);
    pc.mipParams = glm::uvec4(tex.mipLevels, tex.pageSize, tex.gutterSize, static_cast<uint32_t>(m_frameIndex));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_feedbackPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descriptorSets[slot], 0, nullptr);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VTFeedbackPC), &pc);
    vkCmdDispatch(cmd, 4u, 1u, 1u);

    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 1, &barrier, 0, nullptr, 0, nullptr};
    vkCmdPipelineBarrier2(cmd, &dep);
}

void VirtualTextureSystem::updateStatistics() noexcept {
    m_stats.totalVirtualTextures = static_cast<uint32_t>(m_textures.size());
    m_stats.residentPhysicalPages = m_physicalCache.getResidentCount();
    m_stats.freePhysicalPages = m_physicalCache.getFreeCount();
    m_stats.cacheCapacity = m_physicalCache.getCapacity();
    m_stats.physicalCacheMemoryBytes = m_physicalCache.getPhysicalMemoryBytes();
    m_stats.pageTableMemoryBytes = m_pageTable.getMemoryBytes();
    m_stats.feedbackBufferMemoryBytes = (sizeof(GPUFeedbackHeader) + static_cast<uint64_t>(m_config.feedbackCapacity) * sizeof(VTFeedbackRequest)) * kMaxFramesInFlight;
    m_stats.pendingRequestQueueLength = static_cast<uint32_t>(m_requestQueue.size());

    uint64_t totalVirtBytes = 0;
    uint64_t totalVirtPages = 0;
    for (const auto& t : m_textures) {
        totalVirtBytes += t.getTotalVirtualMemoryBytes();
        for (uint32_t m = 0; m < t.mipLevels; ++m) {
            totalVirtPages += static_cast<uint64_t>(t.getPagesX(m)) * t.getPagesY(m);
        }
    }
    m_stats.virtualTextureMemoryBytes = totalVirtBytes;
    m_stats.totalVirtualPages = totalVirtPages;
    m_stats.textureMemorySavedBytes = (totalVirtBytes > m_stats.physicalCacheMemoryBytes)
        ? (totalVirtBytes - m_stats.physicalCacheMemoryBytes) : 0ULL;

    const uint64_t totalSamples = m_stats.cacheHits + m_stats.cacheMisses;
    m_stats.cacheHitRate = (totalSamples > 0u) ? (static_cast<float>(m_stats.cacheHits) / static_cast<float>(totalSamples)) : 0.0f;
    m_stats.cacheUtilization = (m_stats.cacheCapacity > 0u)
        ? (static_cast<float>(m_stats.residentPhysicalPages) / static_cast<float>(m_stats.cacheCapacity)) : 0.0f;
}

} // namespace sidera
