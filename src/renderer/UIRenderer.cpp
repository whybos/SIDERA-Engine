#include "renderer/UIRenderer.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanContext.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace sidera {

namespace {

// Compact 5x7 uppercase/lowercase/digit/punctuation bitmap glyph table for ASCII 32..126.
// Each character is 5 columns (LSB=top row, 7 bits high).
std::array<uint8_t, 5> getGlyph5x7(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    switch (c) {
        case 'A': return {0x7E, 0x11, 0x11, 0x11, 0x7E};
        case 'B': return {0x7F, 0x49, 0x49, 0x49, 0x36};
        case 'C': return {0x3E, 0x41, 0x41, 0x41, 0x22};
        case 'D': return {0x7F, 0x41, 0x41, 0x22, 0x1C};
        case 'E': return {0x7F, 0x49, 0x49, 0x49, 0x41};
        case 'F': return {0x7F, 0x09, 0x09, 0x09, 0x01};
        case 'G': return {0x3E, 0x41, 0x49, 0x49, 0x7A};
        case 'H': return {0x7F, 0x08, 0x08, 0x08, 0x7F};
        case 'I': return {0x00, 0x41, 0x7F, 0x41, 0x00};
        case 'J': return {0x20, 0x40, 0x41, 0x3F, 0x01};
        case 'K': return {0x7F, 0x08, 0x14, 0x22, 0x41};
        case 'L': return {0x7F, 0x40, 0x40, 0x40, 0x40};
        case 'M': return {0x7F, 0x02, 0x0C, 0x02, 0x7F};
        case 'N': return {0x7F, 0x04, 0x08, 0x10, 0x7F};
        case 'O': return {0x3E, 0x41, 0x41, 0x41, 0x3E};
        case 'P': return {0x7F, 0x09, 0x09, 0x09, 0x06};
        case 'Q': return {0x3E, 0x41, 0x51, 0x21, 0x5E};
        case 'R': return {0x7F, 0x09, 0x19, 0x29, 0x46};
        case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
        case 'T': return {0x01, 0x01, 0x7F, 0x01, 0x01};
        case 'U': return {0x3F, 0x40, 0x40, 0x40, 0x3F};
        case 'V': return {0x1F, 0x20, 0x40, 0x20, 0x1F};
        case 'W': return {0x3F, 0x40, 0x38, 0x40, 0x3F};
        case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
        case 'Y': return {0x07, 0x08, 0x70, 0x08, 0x07};
        case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};
        case '0': return {0x3E, 0x51, 0x49, 0x45, 0x3E};
        case '1': return {0x00, 0x42, 0x7F, 0x40, 0x00};
        case '2': return {0x42, 0x61, 0x51, 0x49, 0x46};
        case '3': return {0x21, 0x41, 0x45, 0x4B, 0x31};
        case '4': return {0x18, 0x14, 0x12, 0x7F, 0x10};
        case '5': return {0x27, 0x45, 0x45, 0x45, 0x39};
        case '6': return {0x3C, 0x4A, 0x49, 0x49, 0x30};
        case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
        case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
        case '9': return {0x06, 0x49, 0x49, 0x29, 0x1E};
        case ':': return {0x00, 0x36, 0x36, 0x00, 0x00};
        case '.': return {0x00, 0x60, 0x60, 0x00, 0x00};
        case ',': return {0x00, 0x50, 0x30, 0x00, 0x00};
        case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
        case '+': return {0x08, 0x08, 0x3E, 0x08, 0x08};
        case '/': return {0x20, 0x10, 0x08, 0x04, 0x02};
        case '[': return {0x00, 0x7F, 0x41, 0x41, 0x00};
        case ']': return {0x00, 0x41, 0x41, 0x7F, 0x00};
        case '(': return {0x00, 0x1C, 0x22, 0x41, 0x00};
        case ')': return {0x00, 0x41, 0x22, 0x1C, 0x00};
        case '%': return {0x23, 0x13, 0x08, 0x64, 0x62};
        case '!': return {0x00, 0x00, 0x5F, 0x00, 0x00};
        case '|': return {0x00, 0x00, 0x7F, 0x00, 0x00};
        case '_': return {0x40, 0x40, 0x40, 0x40, 0x40};
        default:  return {0x00, 0x00, 0x00, 0x00, 0x00};
    }
}

std::vector<uint8_t> readBinaryFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const auto sz = f.tellg();
    if (sz <= 0) return {};
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()), sz);
    return data;
}

} // namespace

uint32_t UIRenderer::findMemoryType(VkPhysicalDevice phys, uint32_t typeFilter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return 0;
}

bool UIRenderer::init(const VulkanContext& ctx, VkFormat colorFormat, VkFormat depthFormat) {
    m_device = ctx.getDevice().getLogicalDevice();
    if (m_device == VK_NULL_HANDLE) return false;

    if (!createFontAtlas(ctx)) {
        SIDERA_LOG_WARN("UIRenderer", "Failed to create bitmap font atlas.");
        shutdown();
        return false;
    }
    if (!createVertexBuffer(ctx)) {
        SIDERA_LOG_WARN("UIRenderer", "Failed to create UI dynamic vertex buffer.");
        shutdown();
        return false;
    }
    if (!createPipeline(ctx, colorFormat, depthFormat)) {
        SIDERA_LOG_WARN("UIRenderer", "Failed to create Vulkan 2D UI graphics pipeline (check shaders/ui_quad.*.spv).");
        shutdown();
        return false;
    }

    m_initialized = true;
    SIDERA_LOG_INFO("UIRenderer", "Vulkan 2D Runtime UI Renderer initialized (embedded ASCII font atlas, alpha blending, scissor clipping).");
    return true;
}

void UIRenderer::shutdown() noexcept {
    if (m_device == VK_NULL_HANDLE) return;
    if (m_mappedVertices && m_vertexMemory != VK_NULL_HANDLE) {
        vkUnmapMemory(m_device, m_vertexMemory);
        m_mappedVertices = nullptr;
    }
    if (m_vertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, m_vertexBuffer, nullptr); m_vertexBuffer = VK_NULL_HANDLE; }
    if (m_vertexMemory != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_vertexMemory, nullptr); m_vertexMemory = VK_NULL_HANDLE; }
    if (m_pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(m_device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
    if (m_pipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
    if (m_descPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(m_device, m_descPool, nullptr); m_descPool = VK_NULL_HANDLE; }
    if (m_descLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(m_device, m_descLayout, nullptr); m_descLayout = VK_NULL_HANDLE; }
    if (m_fontSampler != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_fontSampler, nullptr); m_fontSampler = VK_NULL_HANDLE; }
    if (m_fontView != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_fontView, nullptr); m_fontView = VK_NULL_HANDLE; }
    if (m_fontImage != VK_NULL_HANDLE) { vkDestroyImage(m_device, m_fontImage, nullptr); m_fontImage = VK_NULL_HANDLE; }
    if (m_fontMemory != VK_NULL_HANDLE) { vkFreeMemory(m_device, m_fontMemory, nullptr); m_fontMemory = VK_NULL_HANDLE; }
    m_initialized = false;
    m_device = VK_NULL_HANDLE;
}

bool UIRenderer::createFontAtlas(const VulkanContext& ctx) {
    // 16x6 grid of 8x8 cells = 128x48 RGBA8 atlas for ASCII 32..127
    constexpr uint32_t kAtlasW = 128;
    constexpr uint32_t kAtlasH = 48;
    std::vector<uint8_t> pixels(kAtlasW * kAtlasH * 4, 0);

    for (uint32_t ch = 32; ch < 128; ++ch) {
        const uint32_t idx = ch - 32;
        const uint32_t cellX = (idx % 16) * 8;
        const uint32_t cellY = (idx / 16) * 8;
        const auto glyph = getGlyph5x7(static_cast<char>(ch));
        for (uint32_t col = 0; col < 5; ++col) {
            for (uint32_t row = 0; row < 7; ++row) {
                if ((glyph[col] >> row) & 1u) {
                    const uint32_t px = cellX + col + 1;
                    const uint32_t py = cellY + row;
                    const size_t off = (py * kAtlasW + px) * 4;
                    pixels[off + 0] = 255;
                    pixels[off + 1] = 255;
                    pixels[off + 2] = 255;
                    pixels[off + 3] = 255;
                }
            }
        }
    }

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.extent = {kAtlasW, kAtlasH, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &imgInfo, nullptr, &m_fontImage) != VK_SUCCESS) return false;

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(m_device, m_fontImage, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(ctx.getDevice().getPhysicalDevice(), memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_fontMemory) != VK_SUCCESS) return false;
    vkBindImageMemory(m_device, m_fontImage, m_fontMemory, 0);

    // Staging upload
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBufferCreateInfo sbInfo{};
    sbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sbInfo.size = pixels.size();
    sbInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    sbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(m_device, &sbInfo, nullptr, &stagingBuf);
    VkMemoryRequirements sbReq{};
    vkGetBufferMemoryRequirements(m_device, stagingBuf, &sbReq);
    VkMemoryAllocateInfo sbAlloc{};
    sbAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    sbAlloc.allocationSize = sbReq.size;
    sbAlloc.memoryTypeIndex = findMemoryType(ctx.getDevice().getPhysicalDevice(), sbReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(m_device, &sbAlloc, nullptr, &stagingMem);
    vkBindBufferMemory(m_device, stagingBuf, stagingMem, 0);
    void* mapped = nullptr;
    vkMapMemory(m_device, stagingMem, 0, pixels.size(), 0, &mapped);
    std::memcpy(mapped, pixels.data(), pixels.size());
    vkUnmapMemory(m_device, stagingMem);

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = ctx.getDevice().getQueueFamilies().graphicsFamily.value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    vkCreateCommandPool(m_device, &poolInfo, nullptr, &pool);

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = pool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_device, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = m_fontImage;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {kAtlasW, kAtlasH, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuf, m_fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toRead);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx.getDevice().getGraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.getDevice().getGraphicsQueue());

    vkDestroyCommandPool(m_device, pool, nullptr);
    vkDestroyBuffer(m_device, stagingBuf, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_fontImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_fontView) != VK_SUCCESS) return false;

    VkSamplerCreateInfo sampInfo{};
    sampInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter = VK_FILTER_NEAREST;
    sampInfo.minFilter = VK_FILTER_NEAREST;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_device, &sampInfo, nullptr, &m_fontSampler) != VK_SUCCESS) return false;

    return true;
}

bool UIRenderer::createVertexBuffer(const VulkanContext& ctx) {
    const VkDeviceSize size = m_maxVertices * sizeof(UIVertex);
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &info, nullptr, &m_vertexBuffer) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(m_device, m_vertexBuffer, &req);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = findMemoryType(ctx.getDevice().getPhysicalDevice(), req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(m_device, &alloc, nullptr, &m_vertexMemory) != VK_SUCCESS) return false;
    vkBindBufferMemory(m_device, m_vertexBuffer, m_vertexMemory, 0);
    return vkMapMemory(m_device, m_vertexMemory, 0, size, 0, &m_mappedVertices) == VK_SUCCESS;
}

bool UIRenderer::createPipeline(const VulkanContext&, VkFormat colorFormat, VkFormat depthFormat) {
    const auto vertCode = readBinaryFile("shaders/ui_quad.vert.spv");
    const auto fragCode = readBinaryFile("shaders/ui_quad.frag.spv");
    if (vertCode.empty() || fragCode.empty()) return false;

    auto makeShader = [&](const std::vector<uint8_t>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule mod = VK_NULL_HANDLE;
        vkCreateShaderModule(m_device, &ci, nullptr, &mod);
        return mod;
    };
    VkShaderModule vertMod = makeShader(vertCode);
    VkShaderModule fragMod = makeShader(fragCode);
    if (vertMod == VK_NULL_HANDLE || fragMod == VK_NULL_HANDLE) return false;

    // Descriptor set for font atlas
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 1;
    dslInfo.pBindings = &binding;
    vkCreateDescriptorSetLayout(m_device, &dslInfo, nullptr, &m_descLayout);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dpInfo{};
    dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpInfo.maxSets = 1;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes = &poolSize;
    vkCreateDescriptorPool(m_device, &dpInfo, nullptr, &m_descPool);

    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = m_descPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &m_descLayout;
    vkAllocateDescriptorSets(m_device, &dsAlloc, &m_descSet);

    VkDescriptorImageInfo imgDesc{m_fontSampler, m_fontView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgDesc;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    VkPushConstantRange pcRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &m_descLayout;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcRange;
    vkCreatePipelineLayout(m_device, &plInfo, nullptr, &m_pipelineLayout);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName = "main";

    VkVertexInputBindingDescription vBind{0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vAttrs[4]{
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, u)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, r)},
        {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(UIVertex, useFont)}
    };
    VkPipelineVertexInputStateCreateInfo viInfo{};
    viInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    viInfo.vertexBindingDescriptionCount = 1;
    viInfo.pVertexBindingDescriptions = &vBind;
    viInfo.vertexAttributeDescriptionCount = 4;
    viInfo.pVertexAttributeDescriptions = vAttrs;

    VkPipelineInputAssemblyStateCreateInfo iaInfo{};
    iaInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    iaInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vpInfo{};
    vpInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpInfo.viewportCount = 1;
    vpInfo.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rsInfo{};
    rsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rsInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rsInfo.cullMode = VK_CULL_MODE_NONE;
    rsInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rsInfo.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msInfo{};
    msInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dsInfo{};
    dsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dsInfo.depthTestEnable = VK_FALSE;
    dsInfo.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.blendEnable = VK_TRUE;
    cbAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cbAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAtt.colorBlendOp = VK_BLEND_OP_ADD;
    cbAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cbAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cbAtt.alphaBlendOp = VK_BLEND_OP_ADD;
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cbInfo{};
    cbInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbInfo.attachmentCount = 1;
    cbInfo.pAttachments = &cbAtt;

    VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynInfo{};
    dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynInfo.dynamicStateCount = 2;
    dynInfo.pDynamicStates = dynStates;

    VkPipelineRenderingCreateInfo rendInfo{};
    rendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendInfo.colorAttachmentCount = 1;
    rendInfo.pColorAttachmentFormats = &colorFormat;
    rendInfo.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo gpInfo{};
    gpInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpInfo.pNext = &rendInfo;
    gpInfo.stageCount = 2;
    gpInfo.pStages = stages;
    gpInfo.pVertexInputState = &viInfo;
    gpInfo.pInputAssemblyState = &iaInfo;
    gpInfo.pViewportState = &vpInfo;
    gpInfo.pRasterizationState = &rsInfo;
    gpInfo.pMultisampleState = &msInfo;
    gpInfo.pDepthStencilState = &dsInfo;
    gpInfo.pColorBlendState = &cbInfo;
    gpInfo.pDynamicState = &dynInfo;
    gpInfo.layout = m_pipelineLayout;

    const VkResult res = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpInfo, nullptr, &m_pipeline);
    vkDestroyShaderModule(m_device, vertMod, nullptr);
    vkDestroyShaderModule(m_device, fragMod, nullptr);
    return res == VK_SUCCESS;
}

void UIRenderer::pushQuad(std::vector<UIVertex>& verts,
                          float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1,
                          float r, float g, float b, float a,
                          float useFont) const {
    if (verts.size() + 6 > m_maxVertices) return;
    verts.push_back({x,     y,     u0, v0, r, g, b, a, useFont});
    verts.push_back({x + w, y,     u1, v0, r, g, b, a, useFont});
    verts.push_back({x + w, y + h, u1, v1, r, g, b, a, useFont});
    verts.push_back({x,     y,     u0, v0, r, g, b, a, useFont});
    verts.push_back({x + w, y + h, u1, v1, r, g, b, a, useFont});
    verts.push_back({x,     y + h, u0, v1, r, g, b, a, useFont});
}

void UIRenderer::pushText(std::vector<UIVertex>& verts,
                          float x, float y, float scale,
                          const std::string& text,
                          float r, float g, float b, float a) const {
    float cx = x;
    const float cellW = 8.0f * scale;
    const float cellH = 8.0f * scale;
    for (char ch : text) {
        if (ch < 32 || ch >= 127) { cx += cellW; continue; }
        const uint32_t idx = static_cast<uint32_t>(ch - 32);
        const float u0 = static_cast<float>(idx % 16) / 16.0f;
        const float v0 = static_cast<float>(idx / 16) / 6.0f;
        const float u1 = u0 + (1.0f / 16.0f);
        const float v1 = v0 + (1.0f / 6.0f);
        pushQuad(verts, cx, y, cellW, cellH, u0, v0, u1, v1, r, g, b, a, 1.0f);
        cx += 6.5f * scale;
    }
}

void UIRenderer::render(VkCommandBuffer cmd,
                        VkExtent2D extent,
                        const std::vector<sidera::runtime::UIDrawCommand>& drawCommands) {
    if (!m_initialized || drawCommands.empty() || extent.width == 0 || extent.height == 0) return;

    struct Batch {
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        VkRect2D scissor{};
    };
    std::vector<UIVertex> verts;
    verts.reserve(drawCommands.size() * 24);
    std::vector<Batch> batches;

    for (const auto& dc : drawCommands) {
        const uint32_t start = static_cast<uint32_t>(verts.size());
        const auto& r = dc.bounds;
        const auto& c = dc.color;

        if (dc.type == sidera::runtime::UIWidgetType::Panel ||
            dc.type == sidera::runtime::UIWidgetType::ScrollView ||
            dc.type == sidera::runtime::UIWidgetType::Image) {
            pushQuad(verts, r.x, r.y, r.width, r.height, 0, 0, 1, 1, c.r, c.g, c.b, c.a, 0.0f);
        } else if (dc.type == sidera::runtime::UIWidgetType::Button ||
                   dc.type == sidera::runtime::UIWidgetType::InputField) {
            pushQuad(verts, r.x, r.y, r.width, r.height, 0, 0, 1, 1, c.r, c.g, c.b, c.a, 0.0f);
            if (dc.focused) {
                pushQuad(verts, r.x, r.y, r.width, 2.0f, 0, 0, 1, 1, 0.35f, 0.78f, 1.0f, 1.0f, 0.0f);
            }
            if (!dc.text.empty()) {
                pushText(verts, r.x + 8.0f, r.y + (r.height - 14.0f) * 0.5f, 2.0f, dc.text, 0.96f, 0.97f, 1.0f, 1.0f);
            }
        } else if (dc.type == sidera::runtime::UIWidgetType::ProgressBar ||
                   dc.type == sidera::runtime::UIWidgetType::Slider) {
            pushQuad(verts, r.x, r.y, r.width, r.height, 0, 0, 1, 1, 0.08f, 0.10f, 0.13f, 0.90f, 0.0f);
            const float fillW = std::clamp(dc.value, 0.0f, 1.0f) * r.width;
            pushQuad(verts, r.x, r.y, fillW, r.height, 0, 0, 1, 1, c.r, c.g, c.b, c.a, 0.0f);
            if (!dc.text.empty()) {
                pushText(verts, r.x + 6.0f, r.y + 3.0f, 1.6f, dc.text, 1.0f, 1.0f, 1.0f, 1.0f);
            }
        } else if (dc.type == sidera::runtime::UIWidgetType::Checkbox) {
            pushQuad(verts, r.x, r.y, r.height, r.height, 0, 0, 1, 1, 0.14f, 0.18f, 0.24f, 0.95f, 0.0f);
            if (dc.value > 0.5f) {
                pushQuad(verts, r.x + 4.0f, r.y + 4.0f, r.height - 8.0f, r.height - 8.0f, 0, 0, 1, 1, 0.25f, 0.85f, 0.45f, 1.0f, 0.0f);
            }
            if (!dc.text.empty()) {
                pushText(verts, r.x + r.height + 8.0f, r.y + 4.0f, 1.8f, dc.text, 0.95f, 0.95f, 0.98f, 1.0f);
            }
        } else if (dc.type == sidera::runtime::UIWidgetType::Text) {
            if (!dc.text.empty()) {
                pushText(verts, r.x, r.y, 2.0f, dc.text, c.r, c.g, c.b, c.a);
            }
        }

        const uint32_t count = static_cast<uint32_t>(verts.size()) - start;
        if (count == 0) continue;

        VkRect2D scissor{{0, 0}, extent};
        if (dc.hasClip) {
            const int32_t sx = std::max(0, static_cast<int32_t>(dc.clipRect.x));
            const int32_t sy = std::max(0, static_cast<int32_t>(dc.clipRect.y));
            const uint32_t sw = static_cast<uint32_t>(std::max(0.0f, dc.clipRect.width));
            const uint32_t sh = static_cast<uint32_t>(std::max(0.0f, dc.clipRect.height));
            scissor = {{sx, sy}, {std::min(sw, extent.width - sx), std::min(sh, extent.height - sy)}};
        }
        batches.push_back({start, count, scissor});
    }

    if (verts.empty()) return;
    std::memcpy(m_mappedVertices, verts.data(), verts.size() * sizeof(UIVertex));

    VkViewport vp{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descSet, 0, nullptr);

    const float invVp[2] = {1.0f / static_cast<float>(extent.width), 1.0f / static_cast<float>(extent.height)};
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(invVp), invVp);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
    for (const auto& b : batches) {
        vkCmdSetScissor(cmd, 0, 1, &b.scissor);
        vkCmdDraw(cmd, b.vertexCount, 1, b.firstVertex, 0);
    }
}

} // namespace sidera
