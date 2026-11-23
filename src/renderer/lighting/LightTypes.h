#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sidera {

enum class LightType : uint32_t {
    Directional = 0,
    Point = 1,
    Spot = 2
};

// std430-aligned GPU light record (64 bytes)
struct alignas(16) GPULight {
    glm::vec4 positionRadius{0.0f, 0.0f, 0.0f, 10.0f}; // xyz = world pos, w = radius
    glm::vec4 directionType{0.0f, -1.0f, 0.0f, 0.0f};  // xyz = dir, w = LightType
    glm::vec4 colorIntensity{1.0f, 1.0f, 1.0f, 1.0f};  // rgb = color, a = intensity
    glm::vec4 spotParams{0.9f, 0.8f, 1.0f, -1.0f};     // x = cosInner, y = cosOuter, z = enabled, w = shadowIdx
};
static_assert(sizeof(GPULight) == 64, "GPULight must be 64 bytes (std430).");

class LightManager {
public:
    static constexpr uint32_t kMaxLights = 512;

    uint32_t addDirectionalLight(const glm::vec3& direction, const glm::vec3& color, float intensity) {
        if (m_lights.size() >= kMaxLights) return 0xFFFFFFFFu;
        GPULight l{};
        l.directionType = glm::vec4(direction, static_cast<float>(LightType::Directional));
        l.colorIntensity = glm::vec4(color, intensity);
        l.spotParams = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        m_lights.push_back(l);
        m_dirty = true;
        return static_cast<uint32_t>(m_lights.size() - 1u);
    }

    uint32_t addPointLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius) {
        if (m_lights.size() >= kMaxLights) return 0xFFFFFFFFu;
        GPULight l{};
        l.positionRadius = glm::vec4(position, radius);
        l.directionType = glm::vec4(0.0f, -1.0f, 0.0f, static_cast<float>(LightType::Point));
        l.colorIntensity = glm::vec4(color, intensity);
        l.spotParams = glm::vec4(1.0f, 1.0f, 1.0f, -1.0f);
        m_lights.push_back(l);
        m_dirty = true;
        return static_cast<uint32_t>(m_lights.size() - 1u);
    }

    uint32_t addSpotLight(const glm::vec3& position, const glm::vec3& direction,
                          const glm::vec3& color, float intensity, float radius,
                          float innerConeRad, float outerConeRad) {
        if (m_lights.size() >= kMaxLights) return 0xFFFFFFFFu;
        GPULight l{};
        l.positionRadius = glm::vec4(position, radius);
        l.directionType = glm::vec4(direction, static_cast<float>(LightType::Spot));
        l.colorIntensity = glm::vec4(color, intensity);
        l.spotParams = glm::vec4(std::cos(innerConeRad), std::cos(outerConeRad), 1.0f, -1.0f);
        m_lights.push_back(l);
        m_dirty = true;
        return static_cast<uint32_t>(m_lights.size() - 1u);
    }

    void setLightEnabled(uint32_t idx, bool enabled) {
        if (idx < m_lights.size()) {
            m_lights[idx].spotParams.z = enabled ? 1.0f : 0.0f;
            m_dirty = true;
        }
    }

    void removeLight(uint32_t idx) {
        if (idx < m_lights.size()) {
            m_lights.erase(m_lights.begin() + idx);
            m_dirty = true;
        }
    }

    void clear() {
        m_lights.clear();
        m_dirty = true;
    }

    void populateDemoLights(uint32_t dynamicCount) {
        clear();
        addDirectionalLight(glm::vec3(0.45f, -0.80f, -0.40f), glm::vec3(1.0f, 0.95f, 0.85f), 2.6f);
        for (uint32_t i = 0; i < dynamicCount && m_lights.size() < kMaxLights; ++i) {
            const float angle = static_cast<float>(i) * 0.6180339f * 6.28318f;
            const float ring = 6.0f + static_cast<float>(i % 16u) * 3.5f;
            const glm::vec3 pos(std::cos(angle) * ring, 3.0f + static_cast<float>(i % 4u) * 1.5f, std::sin(angle) * ring);
            const glm::vec3 col(0.4f + 0.6f * ((i * 3u) % 5u) / 4.0f,
                                0.4f + 0.6f * ((i * 7u) % 5u) / 4.0f,
                                0.4f + 0.6f * ((i * 11u) % 5u) / 4.0f);
            if ((i % 4u) == 0u) {
                addSpotLight(pos, glm::vec3(0.0f, -1.0f, 0.0f), col, 8.0f, 18.0f, 0.35f, 0.60f);
            } else {
                addPointLight(pos, col, 6.0f, 14.0f);
            }
        }
    }

    [[nodiscard]] const std::vector<GPULight>& getLights() const noexcept { return m_lights; }
    [[nodiscard]] uint32_t getLightCount() const noexcept { return static_cast<uint32_t>(m_lights.size()); }
    [[nodiscard]] bool isDirty() const noexcept { return m_dirty; }
    void clearDirty() noexcept { m_dirty = false; }

private:
    std::vector<GPULight> m_lights;
    bool m_dirty = true;
};

} // namespace sidera
