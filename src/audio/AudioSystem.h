#pragma once

#include "audio/AudioTypes.h"
#include "physics/PhysicsWorld.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera {

// Modular 3D Audio Engine: WAV PCM decoding, Bus Mixer, Voice Stealing,
// 3D Spatialization + Doppler, Reverb Zones, Physics Occlusion & Cue System (Milestone 38).
class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem() { shutdown(); }

    bool init(const AudioSettings& settings = {});
    void shutdown() noexcept;
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    // WAV PCM Decoding & Clip Management
    static bool decodeWAVFile(const std::filesystem::path& path, AudioClip& outClip);
    bool registerClip(const AudioClip& clip);
    bool unregisterClip(const AssetGUID& guid);
    [[nodiscard]] const AudioClip* findClip(const AssetGUID& guid) const noexcept;

    // Mixer & Bus Hierarchy (with feedback-loop protection)
    bool createBus(const std::string& name, const std::string& parentBus = "Master");
    bool setBusVolume(const std::string& name, float volume);
    bool setBusMuted(const std::string& name, bool muted);
    [[nodiscard]] bool wouldCreateBusCycle(const std::string& busName, const std::string& newParent) const;
    [[nodiscard]] float computeEffectiveBusGain(const std::string& busName) const;

    // Audio Listener & Reverb Zones
    void setListener(const AudioListener& listener) noexcept { m_listener = listener; }
    [[nodiscard]] const AudioListener& getListener() const noexcept { return m_listener; }
    uint64_t addReverbZone(const AudioReverbZone& zone);
    [[nodiscard]] float evaluateListenerReverbWet() const noexcept;

    // Audio Sources & Playback
    uint64_t createSource(const AudioSource& desc);
    bool destroySource(uint64_t sourceId);
    AudioSource* findSource(uint64_t sourceId) noexcept;
    bool play(uint64_t sourceId);
    bool pause(uint64_t sourceId);
    bool stop(uint64_t sourceId);
    uint64_t playOneShot(const AssetGUID& clipGuid, const glm::vec3& position, float volume = 1.0f, const std::string& bus = "SFX");

    // Audio Cues / Events
    void registerCue(const AudioCue& cue);
    uint64_t triggerCue(const std::string& cueName, const glm::vec3& position);

    // Update (occlusion raycasts, voice stealing, reverb zones) & Real-Time Stereo Mix
    void update(float dt, const PhysicsWorld* physicsWorld = nullptr);
    void mixFrames(float* outInterleavedStereo, uint32_t frameCount);

    [[nodiscard]] const AudioSettings& getSettings() const noexcept { return m_settings; }
    void setSettings(const AudioSettings& s) noexcept { m_settings = s; m_settings.validate(); }
    [[nodiscard]] const AudioStatistics& getStats() const noexcept { return m_stats; }

private:
    bool m_initialized = false;
    AudioSettings m_settings{};
    AudioStatistics m_stats{};
    AudioListener m_listener{};
    uint64_t m_nextSourceId = 1;
    uint64_t m_nextZoneId = 1;
    uint32_t m_cueCounter = 0;

    std::unordered_map<AssetGUID, AudioClip, AssetGUIDHash> m_clips;
    std::unordered_map<std::string, AudioBus> m_buses;
    std::unordered_map<uint64_t, AudioSource> m_sources;
    std::unordered_map<uint64_t, AudioReverbZone> m_reverbZones;
    std::unordered_map<std::string, AudioCue> m_cues;
};

} // namespace sidera
