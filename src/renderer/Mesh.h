#pragma once

#include "renderer/Vertex.h"
#include "renderer/vulkan/VulkanBuffer.h"

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sidera {

class VulkanDevice;

struct alignas(16) MeshPushConstants {
    glm::mat4 model{1.0f};
};

class Mesh {
public:
    Mesh(const VulkanDevice& device,
         VkCommandPool transferCommandPool,
         const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices,
         std::string_view name = "Mesh");
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) noexcept = default;
    Mesh& operator=(Mesh&&) noexcept = default;

    void bind(VkCommandBuffer cmd) const noexcept;
    void draw(VkCommandBuffer cmd) const noexcept;

    [[nodiscard]] uint32_t getVertexCount() const noexcept { return m_vertexCount; }
    [[nodiscard]] uint32_t getIndexCount() const noexcept { return m_indexCount; }
    [[nodiscard]] std::string_view getName() const noexcept { return m_name; }

    [[nodiscard]] static std::unique_ptr<Mesh> createCube(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        float halfExtent = 0.5f
    );

    [[nodiscard]] static std::unique_ptr<Mesh> createPyramid(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        float halfBase = 0.5f,
        float height = 1.0f
    );

    [[nodiscard]] static std::unique_ptr<Mesh> createGroundGrid(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        float halfExtent = 2.5f,
        uint32_t subdivisions = 6
    );

private:
    std::string m_name;
    std::unique_ptr<VulkanBuffer> m_vertexBuffer;
    std::unique_ptr<VulkanBuffer> m_indexBuffer;
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
};

} // namespace sidera
