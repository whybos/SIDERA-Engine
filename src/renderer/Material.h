#pragma once

#include <glm/vec3.hpp>
#include <cstdint>
#include <functional>

namespace sidera {

// PBR Material definition used for batching identical visual surfaces
struct Material {
    glm::vec3 albedo{1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    float textureStrength = 0.0f;

    [[nodiscard]] constexpr bool operator==(const Material& o) const noexcept {
        return albedo == o.albedo &&
               metallic == o.metallic &&
               roughness == o.roughness &&
               ao == o.ao &&
               textureStrength == o.textureStrength;
    }
};

} // namespace sidera

namespace std {
template <>
struct hash<sidera::Material> {
    size_t operator()(const sidera::Material& m) const noexcept {
        const size_t h1 = std::hash<float>()(m.albedo.r) ^
                         (std::hash<float>()(m.albedo.g) << 1) ^
                         (std::hash<float>()(m.albedo.b) << 2);
        const size_t h2 = std::hash<float>()(m.metallic) ^
                         (std::hash<float>()(m.roughness) << 1);
        const size_t h3 = std::hash<float>()(m.ao) ^
                         (std::hash<float>()(m.textureStrength) << 1);
        return h1 ^ (h2 << 3) ^ (h3 << 6);
    }
};
} // namespace std
