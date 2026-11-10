#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

namespace sidera {

// Represents an observer or camera entity driving world streaming requests
struct StreamingSource {
    uint32_t id = 0;
    std::string name = "MainCamera";
    bool enabled = true;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    glm::vec3 velocity{0.0f};
    float radiusMultiplier = 1.0f;
    float priorityWeight = 1.0f;

    [[nodiscard]] glm::vec3 getPredictedPosition(float predictionSeconds) const noexcept {
        return position + velocity * predictionSeconds;
    }
};

} // namespace sidera
