#include "renderer/vulkan/SamplerManager.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanCheck.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <algorithm>

namespace sidera {

SamplerManager::SamplerManager(const VulkanDevice& device)
    : m_device(device) {
    SIDERA_LOG_INFO("SamplerManager", "Initialized centralized SamplerManager.");
}

SamplerManager::~SamplerManager() {
    clear();
}

void SamplerManager::clear() noexcept {
    const VkDevice dev = m_device.getLogicalDevice();
    if (dev == VK_NULL_HANDLE) {
        m_cache.clear();
        return;
    }

    for (auto& [desc, sampler] : m_cache) {
        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(dev, sampler, nullptr);
        }
    }
    m_cache.clear();
}

VkSampler SamplerManager::getOrCreateSampler(const SamplerDesc& desc) {
    auto it = m_cache.find(desc);
    if (it != m_cache.end()) {
        return it->second;
    }

    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = desc.magFilter;
    info.minFilter = desc.minFilter;
    info.mipmapMode = desc.mipmapMode;
    info.addressModeU = desc.addressModeU;
    info.addressModeV = desc.addressModeV;
    info.addressModeW = desc.addressModeW;
    info.mipLodBias = desc.mipLodBias;

    if (desc.anisotropyEnable && m_device.isSamplerAnisotropyEnabled()) {
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy = std::min(desc.maxAnisotropy, m_device.getProperties().limits.maxSamplerAnisotropy);
    } else {
        info.anisotropyEnable = VK_FALSE;
        info.maxAnisotropy = 1.0f;
    }

    info.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
    info.compareOp = desc.compareOp;
    info.minLod = desc.minLod;
    info.maxLod = desc.maxLod;
    info.borderColor = desc.borderColor;
    info.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSampler(m_device.getLogicalDevice(), &info, nullptr, &sampler),
             "Failed to create VkSampler in SamplerManager");

    m_cache[desc] = sampler;
    SIDERA_LOG_INFO("SamplerManager", "Created and cached VkSampler (Total cached: {}).", m_cache.size());
    return sampler;
}

VkSampler SamplerManager::getLinearRepeatSampler() {
    SamplerDesc desc{};
    desc.minFilter = VK_FILTER_LINEAR;
    desc.magFilter = VK_FILTER_LINEAR;
    desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    desc.anisotropyEnable = true;
    desc.maxAnisotropy = 16.0f;
    desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    desc.minLod = 0.0f;
    desc.maxLod = 16.0f;
    return getOrCreateSampler(desc);
}

VkSampler SamplerManager::getLinearClampSampler() {
    SamplerDesc desc{};
    desc.minFilter = VK_FILTER_LINEAR;
    desc.magFilter = VK_FILTER_LINEAR;
    desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.anisotropyEnable = true;
    desc.maxAnisotropy = 16.0f;
    desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    desc.minLod = 0.0f;
    desc.maxLod = 16.0f;
    return getOrCreateSampler(desc);
}

VkSampler SamplerManager::getNearestClampSampler() {
    SamplerDesc desc{};
    desc.minFilter = VK_FILTER_NEAREST;
    desc.magFilter = VK_FILTER_NEAREST;
    desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.anisotropyEnable = false;
    desc.maxAnisotropy = 1.0f;
    desc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    desc.minLod = 0.0f;
    desc.maxLod = 1.0f;
    return getOrCreateSampler(desc);
}

VkSampler SamplerManager::getShadowComparisonSampler() {
    SamplerDesc desc{};
    desc.minFilter = VK_FILTER_LINEAR;
    desc.magFilter = VK_FILTER_LINEAR;
    desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.anisotropyEnable = false;
    desc.compareEnable = true;
    desc.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    desc.minLod = 0.0f;
    desc.maxLod = 1.0f;
    return getOrCreateSampler(desc);
}

} // namespace sidera
