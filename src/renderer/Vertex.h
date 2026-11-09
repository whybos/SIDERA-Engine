#pragma once

#include <glm/vec3.hpp>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>

namespace sidera {

struct Vertex {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};

    [[nodiscard]] static constexpr VkVertexInputBindingDescription getBindingDescription() noexcept {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    [[nodiscard]] static constexpr std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() noexcept {
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

        // Location 0: position (vec3)
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = static_cast<uint32_t>(offsetof(Vertex, position));

        // Location 1: color (vec3)
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = static_cast<uint32_t>(offsetof(Vertex, color));

        return attributeDescriptions;
    }
};

static_assert(sizeof(Vertex) == 24, "Vertex struct must be tightly packed at 24 bytes (2 x glm::vec3).");
static_assert(offsetof(Vertex, position) == 0, "Vertex::position must be at offset 0.");
static_assert(offsetof(Vertex, color) == 12, "Vertex::color must be at offset 12.");

} // namespace sidera
