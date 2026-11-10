#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace sidera {

class VulkanDevice;

struct SamplerDesc {
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    bool anisotropyEnable = true;
    float maxAnisotropy = 16.0f;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    float minLod = 0.0f;
    float maxLod = 16.0f;
    float mipLodBias = 0.0f;
    VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    bool compareEnable = false;
    VkCompareOp compareOp = VK_COMPARE_OP_NEVER;

    [[nodiscard]] constexpr bool operator==(const SamplerDesc& o) const noexcept {
        return minFilter == o.minFilter &&
               magFilter == o.magFilter &&
               addressModeU == o.addressModeU &&
               addressModeV == o.addressModeV &&
               addressModeW == o.addressModeW &&
               anisotropyEnable == o.anisotropyEnable &&
               maxAnisotropy == o.maxAnisotropy &&
               mipmapMode == o.mipmapMode &&
               minLod == o.minLod &&
               maxLod == o.maxLod &&
               mipLodBias == o.mipLodBias &&
               borderColor == o.borderColor &&
               compareEnable == o.compareEnable &&
               compareOp == o.compareOp;
    }
};

} // namespace sidera

namespace std {
template <>
struct hash<sidera::SamplerDesc> {
    size_t operator()(const sidera::SamplerDesc& s) const noexcept {
        size_t h = std::hash<uint32_t>()(static_cast<uint32_t>(s.minFilter));
        h ^= (std::hash<uint32_t>()(static_cast<uint32_t>(s.magFilter)) << 1);
        h ^= (std::hash<uint32_t>()(static_cast<uint32_t>(s.addressModeU)) << 2);
        h ^= (std::hash<uint32_t>()(static_cast<uint32_t>(s.addressModeV)) << 3);
        h ^= (std::hash<uint32_t>()(static_cast<uint32_t>(s.addressModeW)) << 4);
        h ^= (std::hash<bool>()(s.anisotropyEnable) << 5);
        h ^= (std::hash<float>()(s.maxAnisotropy) << 6);
        h ^= (std::hash<uint32_t>()(static_cast<uint32_t>(s.mipmapMode)) << 7);
        h ^= (std::hash<float>()(s.minLod) << 8);
        h ^= (std::hash<float>()(s.maxLod) << 9);
        h ^= (std::hash<float>()(s.mipLodBias) << 10);
        h ^= (std::hash<uint32_t>()(static_cast<uint32_t>(s.borderColor)) << 11);
        h ^= (std::hash<bool>()(s.compareEnable) << 12);
        h ^= (std::hash<uint32_t>()(static_cast<uint32_t>(s.compareOp)) << 13);
        return h;
    }
};
} // namespace std

namespace sidera {

class SamplerManager {
public:
    explicit SamplerManager(const VulkanDevice& device);
    ~SamplerManager();

    SamplerManager(const SamplerManager&) = delete;
    SamplerManager& operator=(const SamplerManager&) = delete;
    SamplerManager(SamplerManager&&) = delete;
    SamplerManager& operator=(SamplerManager&&) = delete;

    [[nodiscard]] VkSampler getOrCreateSampler(const SamplerDesc& desc);

    // Preconfigured commonly used samplers
    [[nodiscard]] VkSampler getLinearRepeatSampler();
    [[nodiscard]] VkSampler getLinearClampSampler();
    [[nodiscard]] VkSampler getNearestClampSampler();
    [[nodiscard]] VkSampler getShadowComparisonSampler();

    [[nodiscard]] uint32_t getActiveSamplerCount() const noexcept {
        return static_cast<uint32_t>(m_cache.size());
    }

    void clear() noexcept;

private:
    const VulkanDevice& m_device;
    std::unordered_map<SamplerDesc, VkSampler> m_cache;
};

} // namespace sidera
