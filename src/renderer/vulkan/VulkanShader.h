#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace sidera {

class VulkanShader {
public:
    VulkanShader(VkDevice device, const std::filesystem::path& spvFilePath);
    ~VulkanShader();

    VulkanShader(const VulkanShader&) = delete;
    VulkanShader& operator=(const VulkanShader&) = delete;
    VulkanShader(VulkanShader&&) = delete;
    VulkanShader& operator=(VulkanShader&&) = delete;

    [[nodiscard]] VkShaderModule getHandle() const noexcept { return m_shaderModule; }

private:
    [[nodiscard]] static std::filesystem::path resolveShaderPath(const std::filesystem::path& relativeOrAbsPath);
    [[nodiscard]] static std::vector<uint32_t> readSpirvFile(const std::filesystem::path& filePath);

    VkDevice m_device = VK_NULL_HANDLE;
    VkShaderModule m_shaderModule = VK_NULL_HANDLE;
};

} // namespace sidera
