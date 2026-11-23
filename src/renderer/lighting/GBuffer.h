#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>

namespace sidera {

class VulkanDevice;

// Vulkan 1.3 G-Buffer holding Albedo, HDR Normal, Material (Rough/Metal/AO), and HDR Emissive attachments.
class GBuffer {
public:
    static constexpr uint32_t kAttachmentCount = 4; // 0:Albedo, 1:Normal, 2:Material, 3:Emissive

    struct Attachment {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
    };

    GBuffer() = default;
    ~GBuffer();

    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;

    bool init(const VulkanDevice& device, uint32_t width, uint32_t height);
    bool resize(uint32_t width, uint32_t height);
    void destroy() noexcept;

    void transitionToAttachments(VkCommandBuffer cmd) const noexcept;
    void transitionToShaderRead(VkCommandBuffer cmd) const noexcept;

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] uint32_t getWidth() const noexcept { return m_width; }
    [[nodiscard]] uint32_t getHeight() const noexcept { return m_height; }
    [[nodiscard]] const Attachment& getAttachment(uint32_t idx) const noexcept { return m_attachments[idx]; }
    [[nodiscard]] VkSampler getSampler() const noexcept { return m_sampler; }
    [[nodiscard]] uint64_t getMemoryUsageBytes() const noexcept { return m_memoryBytes; }

private:
    bool createAttachment(Attachment& out, VkFormat format, uint32_t width, uint32_t height);
    void destroyAttachment(Attachment& a) noexcept;

    const VulkanDevice* m_device = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint64_t m_memoryBytes = 0;
    bool m_initialized = false;

    std::array<Attachment, kAttachmentCount> m_attachments{};
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace sidera
