#pragma once

#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace sidera {

enum class ToneMapOperator : uint8_t {
    ACES = 0,
    Reinhard = 1,
    Linear = 2
};

enum class PostProcessDebugMode : uint8_t {
    Final = 0,
    HDRScene = 1,
    BloomOnly = 2,
    SSAOOnly = 3
};

[[nodiscard]] constexpr std::string_view getToneMapOperatorName(ToneMapOperator op) noexcept {
    switch (op) {
        case ToneMapOperator::ACES:     return "ACES";
        case ToneMapOperator::Reinhard: return "Reinhard";
        case ToneMapOperator::Linear:   return "Linear (None)";
    }
    return "Unknown";
}

struct PostProcessSettings {
    bool enabled = true;
    bool hdrEnabled = true;

    // Bloom settings
    bool bloomEnabled = true;
    float bloomThreshold = 1.0f;
    float bloomSoftKnee = 0.5f;
    float bloomIntensity = 0.35f;
    uint32_t bloomMipLevels = 4;

    // SSAO settings
    bool ssaoEnabled = true;
    float ssaoRadius = 0.65f;
    float ssaoBias = 0.025f;
    float ssaoIntensity = 1.25f;
    uint32_t ssaoSampleCount = 16;

    // Tone mapping & exposure
    ToneMapOperator toneMapOperator = ToneMapOperator::ACES;
    float exposure = 1.0f;
    float exposureCompensation = 0.0f; // EV stops

    PostProcessDebugMode debugMode = PostProcessDebugMode::Final;

    [[nodiscard]] float getEffectiveExposure() const noexcept {
        return exposure * std::exp2(exposureCompensation);
    }

    void validate() noexcept {
        bloomThreshold = std::clamp(bloomThreshold, 0.0f, 16.0f);
        bloomSoftKnee  = std::clamp(bloomSoftKnee, 0.0f, 2.0f);
        bloomIntensity = std::clamp(bloomIntensity, 0.0f, 4.0f);
        bloomMipLevels = std::clamp(bloomMipLevels, 1u, 6u);

        ssaoRadius      = std::clamp(ssaoRadius, 0.05f, 10.0f);
        ssaoBias        = std::clamp(ssaoBias, 0.0f, 0.5f);
        ssaoIntensity   = std::clamp(ssaoIntensity, 0.0f, 4.0f);
        ssaoSampleCount = std::clamp(ssaoSampleCount, 4u, 32u);

        exposure             = std::clamp(exposure, 0.05f, 16.0f);
        exposureCompensation = std::clamp(exposureCompensation, -8.0f, 8.0f);
    }

    // CPU reference math helpers matching shaders/post_tonemap.comp & post_bloom.comp
    [[nodiscard]] static glm::vec3 evalToneMapACES(const glm::vec3& x) noexcept {
        constexpr float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
        const glm::vec3 num = x * (a * x + glm::vec3(b));
        const glm::vec3 den = x * (c * x + glm::vec3(d)) + glm::vec3(e);
        return glm::clamp(num / den, glm::vec3(0.0f), glm::vec3(1.0f));
    }

    [[nodiscard]] static glm::vec3 evalToneMapReinhard(const glm::vec3& x) noexcept {
        return x / (x + glm::vec3(1.0f));
    }

    [[nodiscard]] static glm::vec3 evalBrightPass(const glm::vec3& color, float threshold, float knee) noexcept {
        const float br = std::max({color.r, color.g, color.b});
        float soft = std::clamp(br - threshold + knee, 0.0f, 2.0f * knee);
        soft = (soft * soft) / std::max(4.0f * knee, 1e-5f);
        const float contrib = std::max(soft, br - threshold) / std::max(br, 1e-5f);
        return color * std::max(contrib, 0.0f);
    }
};

struct PostProcessStatistics {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bloomWidth = 0;
    uint32_t bloomHeight = 0;
    uint32_t ssaoWidth = 0;
    uint32_t ssaoHeight = 0;
    uint64_t totalGpuMemoryBytes = 0;
    bool hdrFormatActive = false;
};

} // namespace sidera
