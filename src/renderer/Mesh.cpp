#include "renderer/Mesh.h"
#include "core/Log.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sidera {

Mesh::Mesh(const VulkanDevice& device,
           VkCommandPool transferCommandPool,
           const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices,
           std::string_view name)
    : m_name(name)
    , m_vertexCount(static_cast<uint32_t>(vertices.size()))
    , m_indexCount(static_cast<uint32_t>(indices.size())) {
    if (vertices.empty() || indices.empty()) {
        throw std::invalid_argument("Mesh requires non-empty vertices and indices.");
    }

    const VkDeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();
    const VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

    m_vertexBuffer = VulkanBuffer::createDeviceLocalBuffer(
        device,
        transferCommandPool,
        vertices.data(),
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    );

    m_indexBuffer = VulkanBuffer::createDeviceLocalBuffer(
        device,
        transferCommandPool,
        indices.data(),
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    );

    SIDERA_LOG_INFO(
        "Mesh",
        "Created 3D Mesh '{}' ({} vertices [{} B], {} indices [{} B]).",
        m_name,
        m_vertexCount,
        vertexBufferSize,
        m_indexCount,
        indexBufferSize
    );
}

Mesh::~Mesh() {
    if (m_vertexBuffer || m_indexBuffer) {
        SIDERA_LOG_INFO("Mesh", "Destroying 3D Mesh '{}'...", m_name);
        m_indexBuffer.reset();
        m_vertexBuffer.reset();
    }
}

void Mesh::bind(VkCommandBuffer cmd) const noexcept {
    const VkBuffer vertexBuffers[] = {m_vertexBuffer->getHandle()};
    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::draw(VkCommandBuffer cmd) const noexcept {
    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

std::unique_ptr<Mesh> Mesh::createCube(const VulkanDevice& device,
                                       VkCommandPool transferCommandPool,
                                       float h) {
    std::vector<Vertex> vertices = {
        // Front face (+Z) - Coral Red
        {{-h, -h,  h}, {0.95f, 0.32f, 0.28f}, {0.0f, 1.0f}},
        {{ h, -h,  h}, {0.98f, 0.45f, 0.32f}, {1.0f, 1.0f}},
        {{ h,  h,  h}, {1.00f, 0.58f, 0.38f}, {1.0f, 0.0f}},
        {{-h,  h,  h}, {0.95f, 0.40f, 0.35f}, {0.0f, 0.0f}},

        // Back face (-Z) - Deep Azure
        {{ h, -h, -h}, {0.20f, 0.48f, 0.92f}, {0.0f, 1.0f}},
        {{-h, -h, -h}, {0.18f, 0.40f, 0.85f}, {1.0f, 1.0f}},
        {{-h,  h, -h}, {0.28f, 0.62f, 0.98f}, {1.0f, 0.0f}},
        {{ h,  h, -h}, {0.32f, 0.68f, 1.00f}, {0.0f, 0.0f}},

        // Right face (+X) - Emerald Teal
        {{ h, -h,  h}, {0.16f, 0.80f, 0.56f}, {0.0f, 1.0f}},
        {{ h, -h, -h}, {0.14f, 0.72f, 0.50f}, {1.0f, 1.0f}},
        {{ h,  h, -h}, {0.26f, 0.92f, 0.68f}, {1.0f, 0.0f}},
        {{ h,  h,  h}, {0.30f, 0.96f, 0.72f}, {0.0f, 0.0f}},

        // Left face (-X) - Royal Violet
        {{-h, -h, -h}, {0.62f, 0.32f, 0.92f}, {0.0f, 1.0f}},
        {{-h, -h,  h}, {0.68f, 0.38f, 0.96f}, {1.0f, 1.0f}},
        {{-h,  h,  h}, {0.78f, 0.52f, 1.00f}, {1.0f, 0.0f}},
        {{-h,  h, -h}, {0.72f, 0.45f, 0.98f}, {0.0f, 0.0f}},

        // Top face (+Y) - Golden Amber
        {{-h,  h,  h}, {0.98f, 0.80f, 0.24f}, {0.0f, 1.0f}},
        {{ h,  h,  h}, {1.00f, 0.86f, 0.32f}, {1.0f, 1.0f}},
        {{ h,  h, -h}, {0.96f, 0.76f, 0.22f}, {1.0f, 0.0f}},
        {{-h,  h, -h}, {0.92f, 0.72f, 0.20f}, {0.0f, 0.0f}},

        // Bottom face (-Y) - Dark Slate
        {{-h, -h, -h}, {0.22f, 0.26f, 0.34f}, {0.0f, 1.0f}},
        {{ h, -h, -h}, {0.25f, 0.30f, 0.38f}, {1.0f, 1.0f}},
        {{ h, -h,  h}, {0.28f, 0.34f, 0.42f}, {1.0f, 0.0f}},
        {{-h, -h,  h}, {0.24f, 0.28f, 0.36f}, {0.0f, 0.0f}}
    };

    // Per-face outward normals (face order: +Z, -Z, +X, -X, +Y, -Y)
    const glm::vec3 faceNormals[6] = {
        { 0.0f,  0.0f,  1.0f}, { 0.0f,  0.0f, -1.0f},
        { 1.0f,  0.0f,  0.0f}, {-1.0f,  0.0f,  0.0f},
        { 0.0f,  1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f}
    };
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].normal = faceNormals[i / 4];
    }

    std::vector<uint32_t> indices;
    indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face) {
        const uint32_t base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
        indices.push_back(base + 0);
    }

    return std::make_unique<Mesh>(device, transferCommandPool, vertices, indices, "CubeMesh");
}

std::unique_ptr<Mesh> Mesh::createPyramid(const VulkanDevice& device,
                                          VkCommandPool transferCommandPool,
                                          float halfBase,
                                          float height) {
    const float yMin = -height * 0.5f;
    const float yMax =  height * 0.5f;

    std::vector<Vertex> vertices = {
        // Front face (+Z)
        {{-halfBase, yMin,  halfBase}, {0.20f, 0.75f, 0.95f}, {0.0f, 1.0f}},
        {{ halfBase, yMin,  halfBase}, {0.25f, 0.85f, 0.98f}, {1.0f, 1.0f}},
        {{ 0.0f,     yMax,  0.0f    }, {0.95f, 0.98f, 1.00f}, {0.5f, 0.0f}},

        // Right face (+X)
        {{ halfBase, yMin,  halfBase}, {0.15f, 0.55f, 0.88f}, {0.0f, 1.0f}},
        {{ halfBase, yMin, -halfBase}, {0.12f, 0.48f, 0.82f}, {1.0f, 1.0f}},
        {{ 0.0f,     yMax,  0.0f    }, {0.85f, 0.92f, 1.00f}, {0.5f, 0.0f}},

        // Back face (-Z)
        {{ halfBase, yMin, -halfBase}, {0.35f, 0.30f, 0.85f}, {0.0f, 1.0f}},
        {{-halfBase, yMin, -halfBase}, {0.42f, 0.35f, 0.90f}, {1.0f, 1.0f}},
        {{ 0.0f,     yMax,  0.0f    }, {0.88f, 0.85f, 1.00f}, {0.5f, 0.0f}},

        // Left face (-X)
        {{-halfBase, yMin, -halfBase}, {0.55f, 0.35f, 0.92f}, {0.0f, 1.0f}},
        {{-halfBase, yMin,  halfBase}, {0.62f, 0.42f, 0.96f}, {1.0f, 1.0f}},
        {{ 0.0f,     yMax,  0.0f    }, {0.92f, 0.88f, 1.00f}, {0.5f, 0.0f}},

        // Bottom base (-Y)
        {{-halfBase, yMin, -halfBase}, {0.18f, 0.24f, 0.35f}, {0.0f, 1.0f}},
        {{ halfBase, yMin, -halfBase}, {0.20f, 0.26f, 0.38f}, {1.0f, 1.0f}},
        {{ halfBase, yMin,  halfBase}, {0.22f, 0.28f, 0.40f}, {1.0f, 0.0f}},
        {{-halfBase, yMin,  halfBase}, {0.20f, 0.25f, 0.36f}, {0.0f, 0.0f}}
    };

    // Flat normals: 4 side triangles (3 verts each) from the cross product, then the -Y base quad
    for (size_t tri = 0; tri < 4; ++tri) {
        const glm::vec3 p0 = vertices[tri * 3 + 0].position;
        const glm::vec3 p1 = vertices[tri * 3 + 1].position;
        const glm::vec3 p2 = vertices[tri * 3 + 2].position;
        const glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        for (size_t k = 0; k < 3; ++k) {
            vertices[tri * 3 + k].normal = n;
        }
    }
    for (size_t k = 12; k < 16; ++k) {
        vertices[k].normal = {0.0f, -1.0f, 0.0f};
    }

    const std::vector<uint32_t> indices = {
        0,  1,  2,
        3,  4,  5,
        6,  7,  8,
        9, 10, 11,
        12, 13, 14, 14, 15, 12
    };

    return std::make_unique<Mesh>(device, transferCommandPool, vertices, indices, "PyramidMesh");
}

std::unique_ptr<Mesh> Mesh::createGroundGrid(const VulkanDevice& device,
                                             VkCommandPool transferCommandPool,
                                             float halfExtent,
                                             uint32_t subdivisions) {
    if (subdivisions == 0) {
        subdivisions = 1;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const float step = (halfExtent * 2.0f) / static_cast<float>(subdivisions);

    for (uint32_t gz = 0; gz < subdivisions; ++gz) {
        for (uint32_t gx = 0; gx < subdivisions; ++gx) {
            const float x0 = -halfExtent + static_cast<float>(gx) * step;
            const float z0 = -halfExtent + static_cast<float>(gz) * step;
            const float x1 = x0 + step;
            const float z1 = z0 + step;

            const bool isEvenTile = ((gx + gz) % 2) == 0;
            const glm::vec3 c0 = isEvenTile ? glm::vec3{0.12f, 0.19f, 0.28f} : glm::vec3{0.17f, 0.26f, 0.38f};
            const glm::vec3 c1 = isEvenTile ? glm::vec3{0.14f, 0.22f, 0.32f} : glm::vec3{0.20f, 0.30f, 0.43f};

            const uint32_t base = static_cast<uint32_t>(vertices.size());
            // Counter-clockwise when viewed from +Y (above)
            vertices.push_back({{x0, 0.0f, z1}, c0, {0.0f, 1.0f}});
            vertices.push_back({{x1, 0.0f, z1}, c1, {1.0f, 1.0f}});
            vertices.push_back({{x1, 0.0f, z0}, c0, {1.0f, 0.0f}});
            vertices.push_back({{x0, 0.0f, z0}, c1, {0.0f, 0.0f}});

            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
            indices.push_back(base + 0);
        }
    }

    return std::make_unique<Mesh>(device, transferCommandPool, vertices, indices, "GroundGridMesh");
}

std::unique_ptr<Mesh> Mesh::createSphere(const VulkanDevice& device,
                                         VkCommandPool transferCommandPool,
                                         float radius,
                                         uint32_t stacks,
                                         uint32_t slices) {
    stacks = std::max(stacks, 3u);
    slices = std::max(slices, 3u);

    constexpr float kPi = 3.14159265358979323846f;

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<size_t>(stacks + 1) * (slices + 1));

    for (uint32_t i = 0; i <= stacks; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(stacks);
        const float theta = v * kPi;  // 0 = north pole (+Y), pi = south pole
        for (uint32_t j = 0; j <= slices; ++j) {
            const float u = static_cast<float>(j) / static_cast<float>(slices);
            const float phi = u * 2.0f * kPi;

            const glm::vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            Vertex vtx{};
            vtx.position = n * radius;
            vtx.normal = n;   // analytical smooth normal
            vtx.color = {1.0f, 1.0f, 1.0f};
            vtx.uv = {u * 2.0f, v};
            vertices.push_back(vtx);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(stacks) * slices * 6);
    const uint32_t stride = slices + 1;
    for (uint32_t i = 0; i < stacks; ++i) {
        for (uint32_t j = 0; j < slices; ++j) {
            const uint32_t a = i * stride + j;
            const uint32_t b = (i + 1) * stride + j;
            const uint32_t c = (i + 1) * stride + j + 1;
            const uint32_t d = i * stride + j + 1;
            // Counter-clockwise when viewed from outside
            indices.insert(indices.end(), {a, c, b, a, d, c});
        }
    }

    return std::make_unique<Mesh>(device, transferCommandPool, vertices, indices, "SphereMesh");
}

} // namespace sidera