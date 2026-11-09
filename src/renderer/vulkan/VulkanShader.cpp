#include "renderer/vulkan/VulkanShader.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanCheck.h"

#include <format>
#include <fstream>
#include <stdexcept>

namespace sidera {

namespace {
constexpr uint32_t kSpirvMagicNumber = 0x07230203U;
} // namespace

VulkanShader::VulkanShader(VkDevice device, const std::filesystem::path& spvFilePath)
    : m_device(device) {
    const std::filesystem::path resolvedPath = resolveShaderPath(spvFilePath);
    const std::vector<uint32_t> spirvCode = readSpirvFile(resolvedPath);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
    createInfo.pCode = spirvCode.data();

    VK_CHECK(
        vkCreateShaderModule(m_device, &createInfo, nullptr, &m_shaderModule),
        "Failed to create Vulkan shader module"
    );

    SIDERA_LOG_INFO(
        "Shader",
        "Created shader module from '{}' ({} bytes).",
        resolvedPath.filename().string(),
        createInfo.codeSize
    );
}

VulkanShader::~VulkanShader() {
    if (m_shaderModule != VK_NULL_HANDLE) {
        SIDERA_LOG_INFO("Shader", "Destroying Vulkan shader module...");
        vkDestroyShaderModule(m_device, m_shaderModule, nullptr);
        m_shaderModule = VK_NULL_HANDLE;
    }
}

std::filesystem::path VulkanShader::resolveShaderPath(const std::filesystem::path& relativeOrAbsPath) {
    std::error_code ec;
    if (std::filesystem::exists(relativeOrAbsPath, ec)) {
        return relativeOrAbsPath;
    }

#if defined(SIDERA_SHADER_BINARY_DIR)
    const std::filesystem::path buildShaderPath =
        std::filesystem::path(SIDERA_SHADER_BINARY_DIR) / relativeOrAbsPath.filename();
    if (std::filesystem::exists(buildShaderPath, ec)) {
        return buildShaderPath;
    }
#endif

    throw std::runtime_error(
        std::format("Shader SPIR-V file not found: '{}'", relativeOrAbsPath.string())
    );
}

std::vector<uint32_t> VulkanShader::readSpirvFile(const std::filesystem::path& filePath) {
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            std::format("Failed to open SPIR-V shader file: '{}'", filePath.string())
        );
    }

    const std::streampos endPos = file.tellg();
    if (endPos <= 0) {
        throw std::runtime_error(
            std::format("SPIR-V shader file is empty or unreadable: '{}'", filePath.string())
        );
    }

    const auto fileSize = static_cast<size_t>(endPos);
    if (fileSize % sizeof(uint32_t) != 0) {
        throw std::runtime_error(
            std::format("Invalid SPIR-V shader file size ({} bytes is not 4-byte aligned): '{}'",
                        fileSize, filePath.string())
        );
    }

    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));

    if (!file) {
        throw std::runtime_error(
            std::format("Failed reading SPIR-V bytecode from file: '{}'", filePath.string())
        );
    }

    if (buffer.empty() || buffer[0] != kSpirvMagicNumber) {
        throw std::runtime_error(
            std::format("Invalid SPIR-V magic number in shader file: '{}'", filePath.string())
        );
    }

    return buffer;
}

} // namespace sidera
