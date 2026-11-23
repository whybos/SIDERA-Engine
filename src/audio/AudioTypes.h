#pragma once

#include "assets/AssetTypes.h"
#include "scene/SceneTypes.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sidera {

enum class AttenuationModel : uint32_t {
    Linear = 0,
    Inverse = 1,
    Exponential = 2
};

enum class AudioPlaybackState : uint32_t {
    Stopped = 0,
    Playing = 1,
    Paused = 2
};

struct AudioSettings {
    bool enabled = true;
    float masterVolume = 1.0f;
    uint32_t sampleRate = 48000u;
    uint32_t channels = 2u;
    uint32_t bufferFrames = 512u;
    uint32_t maxActiveVoices = 32u;
    float speedOfSound = 343.0f;
    float dopplerScale = 1.0f;
    bool spatialAudioEnabled = true;
    bool dopplerEnabled = true;
    bool occlusionEnabled = true;
    bool reverbEnabled = true;

    void validate() noexcept {
        masterVolume = std::clamp(masterVolume, 0.0f, 2.0f);
        sampleRate = std::clamp(sampleRate, 8000u, 192000u);
        channels = std::clamp(channels, 1u, 2u);
        bufferFrames = std::clamp(bufferFrames, 64u, 4096u);
        maxActiveVoices = std::clamp(maxActiveVoices, 1u, 256u);
        speedOfSound = std::clamp(speedOfSound, 50.0f, 1500.0f);
        dopplerScale = std::clamp(dopplerScale, 0.0f, 4.0f);
    }
};

struct AudioClip {
    AssetGUID guid{};
    std::string name;
    uint32_t sampleRate = 44100u;
    uint32_t channels = 1u;
    uint32_t bitsPerSample = 16u;
    std::vector<float> samples; // interleaved normalized [-1, 1]

    [[nodiscard]] uint64_t frameCount() const noexcept {
        return (channels > 0u) ? (samples.size() / channels) : 0ull;
    }
    [[nodiscard]] double durationSeconds() const noexcept {
        return (sampleRate > 0u) ? (static_cast<double>(frameCount()) / static_cast<double>(sampleRate)) : 0.0;
    }
    [[nodiscard]] uint64_t memoryBytes() const noexcept {
        return samples.size() * sizeof(float);
    }
};

struct AudioBus {
    std::string name;
    std::string parentBus; // empty for Master
    float volume = 1.0f;
    bool muted = false;
    bool solo = false;
    float lowPassCutoffHz = 20000.0f;
    float reverbWet = 0.0f;
};

struct AudioSource {
    uint64_t id = 0;
    EntityID entityId = kInvalidEntityID;
    AssetGUID clipGuid{};
    std::string busName = "SFX";
    AudioPlaybackState state = AudioPlaybackState::Stopped;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool looping = false;
    bool muted = false;
    bool spatialized = true;
    int32_t priority = 0;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float minDistance = 1.0f;
    float maxDistance = 50.0f;
    float rolloff = 1.0f;
    AttenuationModel attenuation = AttenuationModel::Inverse;
    double playbackCursorFrames = 0.0;
    float occlusionFactor = 0.0f; // 0 = clear, 1 = occluded
    bool oneShot = false;
};

struct AudioListener {
    EntityID entityId = kInvalidEntityID;
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    bool active = true;
    int32_t priority = 0;
};

struct AudioReverbZone {
    uint64_t id = 0;
    glm::vec3 position{0.0f};
    float radius = 20.0f;
    float blendDistance = 5.0f;
    float reverbWet = 0.4f;
    int32_t priority = 0;
    bool enabled = true;
};

struct AudioCue {
    std::string name;
    std::vector<AssetGUID> clipVariants;
    std::string busName = "SFX";
    float volumeMin = 0.9f;
    float volumeMax = 1.0f;
    float pitchMin = 0.95f;
    float pitchMax = 1.05f;
    int32_t priority = 0;
};

struct AudioStatistics {
    uint32_t activeVoices = 0;
    uint32_t virtualizedVoices = 0;
    uint32_t loadedClips = 0;
    uint64_t totalDecodedBytes = 0;
    uint32_t activeZones = 0;
    uint32_t underrunCount = 0;
    double lastMixCpuMs = 0.0;
};

// --- Spatial Audio & DSP Math ---

inline float evalDistanceAttenuation(float distance, float minDist, float maxDist,
                                     float rolloff, AttenuationModel model) noexcept {
    const float dMin = std::max(minDist, 0.01f);
    const float dMax = std::max(maxDist, dMin + 0.01f);
    const float d = std::clamp(distance, dMin, dMax);
    switch (model) {
        case AttenuationModel::Linear:
            return std::clamp(1.0f - rolloff * (d - dMin) / (dMax - dMin), 0.0f, 1.0f);
        case AttenuationModel::Inverse:
            return std::clamp(dMin / (dMin + rolloff * (d - dMin)), 0.0f, 1.0f);
        case AttenuationModel::Exponential:
            return std::clamp(std::pow(d / dMin, -std::max(rolloff, 0.0f)), 0.0f, 1.0f);
    }
    return 1.0f;
}

inline float evalDopplerPitch(const glm::vec3& listenerPos, const glm::vec3& listenerVel,
                              const glm::vec3& sourcePos, const glm::vec3& sourceVel,
                              float speedOfSound, float dopplerScale) noexcept {
    if (dopplerScale <= 0.0f) return 1.0f;
    const glm::vec3 toSrc = sourcePos - listenerPos;
    const float dist = glm::length(toSrc);
    if (dist < 1e-4f) return 1.0f;
    const glm::vec3 dir = toSrc / dist;
    const float vL = glm::dot(listenerVel, dir) * dopplerScale;
    const float vS = glm::dot(sourceVel, dir) * dopplerScale;
    const float c = std::max(speedOfSound, 50.0f);
    const float num = std::max(c + vL, 1.0f);
    const float den = std::max(c + vS, 1.0f);
    return std::clamp(num / den, 0.5f, 2.0f);
}

inline void evalStereoPanGains(const glm::vec3& listenerPos, const glm::vec3& listenerRight,
                               const glm::vec3& sourcePos, float& outLeft, float& outRight) noexcept {
    const glm::vec3 toSrc = sourcePos - listenerPos;
    const float dist = glm::length(toSrc);
    if (dist < 1e-4f) {
        outLeft = 0.7071f;
        outRight = 0.7071f;
        return;
    }
    const float pan = std::clamp(glm::dot(toSrc / dist, glm::normalize(listenerRight)), -1.0f, 1.0f);
    const float angle = (pan + 1.0f) * 0.25f * 3.14159265359f; // [0, pi/2] constant-power pan
    outLeft = std::cos(angle);
    outRight = std::sin(angle);
}

} // namespace sidera
