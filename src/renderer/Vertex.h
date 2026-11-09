#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>

namespace sidera {

struct Vertex {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    glm::vec2 uv{0.0f, 0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};

    [[nodiscard]] static constexpr VkVertexInputBindingDescription getBindingDescription() noexcept {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    [[nodiscard]] static constexpr std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() noexcept {
        std::array<VkVertexInputAttributeDescription, 4> a{};

        a[0].binding = 0;
        a[0].location = 0;
        a[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        a[0].offset = static_cast<uint32_t>(offsetof(Vertex, position));

        a[1].binding = 0;
        a[1].location = 1;
        a[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        a[1].offset = static_cast<uint32_t>(offsetof(Vertex, color));

        a[2].binding = 0;
        a[2].location = 2;
        a[2].format = VK_FORMAT_R32G32_SFLOAT;
        a[2].offset = static_cast<uint32_t>(offsetof(Vertex, uv));

        a[3].binding = 0;
        a[3].location = 3;
        a[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        a[3].offset = static_cast<uint32_t>(offsetof(Vertex, normal));

        return a;
    }
};

static_assert(sizeof(Vertex) == 44, "Vertex must be tightly packed at 44 bytes.");
static_assert(offsetof(Vertex, position) == 0, "Vertex::position must be at offset 0.");
static_assert(offsetof(Vertex, color) == 12, "Vertex::color must be at offset 12.");
static_assert(offsetof(Vertex, uv) == 24, "Vertex::uv must be at offset 24.");
static_assert(offsetof(Vertex, normal) == 32, "Vertex::normal must be at offset 32.");

} // namespace sidera
