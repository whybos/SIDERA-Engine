#include "audio/AudioSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>

namespace sidera {

bool AudioSystem::init(const AudioSettings& settings) {
    shutdown();
    m_settings = settings;
    m_settings.validate();

    // Create default reusable bus hierarchy
    m_buses["Master"] = {"Master", "", 1.0f, false, false, 20000.0f, 0.0f};
    createBus("Music", "Master");
    createBus("SFX", "Master");
    createBus("Ambient", "Master");
    createBus("Voice", "Master");
    createBus("UI", "Master");

    m_initialized = true;
    return true;
}

void AudioSystem::shutdown() noexcept {
    m_sources.clear();
    m_clips.clear();
    m_buses.clear();
    m_reverbZones.clear();
    m_cues.clear();
    m_stats = {};
    m_initialized = false;
}

bool AudioSystem::decodeWAVFile(const std::filesystem::path& path, AudioClip& outClip) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    uint8_t riff[12]{};
    if (!f.read(reinterpret_cast<char*>(riff), 12) ||
        std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        return false;
    }

    uint16_t audioFormat = 0, channels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    std::vector<uint8_t> rawData;

    while (f.good()) {
        char chunkId[4]{};
        uint32_t chunkSize = 0;
        if (!f.read(chunkId, 4) || !f.read(reinterpret_cast<char*>(&chunkSize), 4)) break;
        if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
            std::vector<uint8_t> fmt(chunkSize);
            if (!f.read(reinterpret_cast<char*>(fmt.data()), chunkSize)) return false;
            audioFormat = uint16_t(fmt[0]) | (uint16_t(fmt[1]) << 8);
            channels = uint16_t(fmt[2]) | (uint16_t(fmt[3]) << 8);
            sampleRate = uint32_t(fmt[4]) | (uint32_t(fmt[5]) << 8) | (uint32_t(fmt[6]) << 16) | (uint32_t(fmt[7]) << 24);
            bitsPerSample = uint16_t(fmt[14]) | (uint16_t(fmt[15]) << 8);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            rawData.resize(chunkSize);
            if (!f.read(reinterpret_cast<char*>(rawData.data()), chunkSize)) return false;
            break;
        } else {
            f.seekg(chunkSize, std::ios::cur);
        }
    }

    if (audioFormat != 1 || channels == 0 || sampleRate == 0 || rawData.empty()) return false;
    if (bitsPerSample != 8 && bitsPerSample != 16) return false;

    outClip.sampleRate = sampleRate;
    outClip.channels = channels;
    outClip.bitsPerSample = bitsPerSample;
    outClip.samples.clear();

    if (bitsPerSample == 16) {
        const size_t count = rawData.size() / 2;
        outClip.samples.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const int16_t s = static_cast<int16_t>(uint16_t(rawData[i * 2]) | (uint16_t(rawData[i * 2 + 1]) << 8));
            outClip.samples[i] = static_cast<float>(s) / 32768.0f;
        }
    } else {
        outClip.samples.resize(rawData.size());
        for (size_t i = 0; i < rawData.size(); ++i) {
            outClip.samples[i] = (static_cast<float>(rawData[i]) - 128.0f) / 128.0f;
        }
    }
    return true;
}

bool AudioSystem::registerClip(const AudioClip& clip) {
    if (!clip.guid.isValid() || clip.samples.empty()) return false;
    m_clips[clip.guid] = clip;
    m_stats.loadedClips = static_cast<uint32_t>(m_clips.size());
    uint64_t total = 0;
    for (const auto& [g, c] : m_clips) total += c.memoryBytes();
    m_stats.totalDecodedBytes = total;
    return true;
}

bool AudioSystem::unregisterClip(const AssetGUID& guid) {
    if (m_clips.erase(guid) == 0) return false;
    m_stats.loadedClips = static_cast<uint32_t>(m_clips.size());
    return true;
}

const AudioClip* AudioSystem::findClip(const AssetGUID& guid) const noexcept {
    auto it = m_clips.find(guid);
    return (it != m_clips.end()) ? &it->second : nullptr;
}

bool AudioSystem::wouldCreateBusCycle(const std::string& busName, const std::string& newParent) const {
    if (newParent.empty()) return false;
    if (busName == newParent) return true;
    std::string curr = newParent;
    uint32_t guard = 0;
    while (!curr.empty() && guard++ < 64u) {
        if (curr == busName) return true;
        auto it = m_buses.find(curr);
        if (it == m_buses.end()) break;
        curr = it->second.parentBus;
    }
    return false;
}

bool AudioSystem::createBus(const std::string& name, const std::string& parentBus) {
    if (name.empty() || wouldCreateBusCycle(name, parentBus)) return false;
    if (!parentBus.empty() && m_buses.find(parentBus) == m_buses.end()) return false;
    m_buses[name] = {name, parentBus, 1.0f, false, false, 20000.0f, 0.0f};
    return true;
}

bool AudioSystem::setBusVolume(const std::string& name, float volume) {
    auto it = m_buses.find(name);
    if (it == m_buses.end()) return false;
    it->second.volume = std::clamp(volume, 0.0f, 2.0f);
    return true;
}

bool AudioSystem::setBusMuted(const std::string& name, bool muted) {
    auto it = m_buses.find(name);
    if (it == m_buses.end()) return false;
    it->second.muted = muted;
    return true;
}

float AudioSystem::computeEffectiveBusGain(const std::string& busName) const {
    float gain = m_settings.masterVolume;
    std::string curr = busName;
    uint32_t guard = 0;
    while (!curr.empty() && guard++ < 64u) {
        auto it = m_buses.find(curr);
        if (it == m_buses.end()) break;
        if (it->second.muted) return 0.0f;
        gain *= it->second.volume;
        curr = it->second.parentBus;
    }
    return gain;
}

uint64_t AudioSystem::addReverbZone(const AudioReverbZone& zone) {
    AudioReverbZone z = zone;
    z.id = m_nextZoneId++;
    m_reverbZones[z.id] = z;
    m_stats.activeZones = static_cast<uint32_t>(m_reverbZones.size());
    return z.id;
}

float AudioSystem::evaluateListenerReverbWet() const noexcept {
    if (!m_settings.reverbEnabled) return 0.0f;
    float bestWet = 0.0f;
    int32_t bestPrio = -1000000;
    for (const auto& [id, z] : m_reverbZones) {
        if (!z.enabled) continue;
        const float dist = glm::length(m_listener.position - z.position);
        if (dist >= z.radius) continue;
        const float w = std::clamp((z.radius - dist) / std::max(z.blendDistance, 0.1f), 0.0f, 1.0f);
        if (z.priority >= bestPrio) {
            bestPrio = z.priority;
            bestWet = z.reverbWet * w;
        }
    }
    return std::clamp(bestWet, 0.0f, 1.0f);
}

uint64_t AudioSystem::createSource(const AudioSource& desc) {
    AudioSource s = desc;
    s.id = m_nextSourceId++;
    s.volume = std::clamp(s.volume, 0.0f, 2.0f);
    s.pitch = std::clamp(s.pitch, 0.25f, 4.0f);
    m_sources[s.id] = s;
    return s.id;
}

bool AudioSystem::destroySource(uint64_t sourceId) {
    return m_sources.erase(sourceId) > 0;
}

AudioSource* AudioSystem::findSource(uint64_t sourceId) noexcept {
    auto it = m_sources.find(sourceId);
    return (it != m_sources.end()) ? &it->second : nullptr;
}

bool AudioSystem::play(uint64_t sourceId) {
    if (auto* s = findSource(sourceId)) {
        s->state = AudioPlaybackState::Playing;
        return true;
    }
    return false;
}

bool AudioSystem::pause(uint64_t sourceId) {
    if (auto* s = findSource(sourceId)) {
        if (s->state == AudioPlaybackState::Playing) s->state = AudioPlaybackState::Paused;
        return true;
    }
    return false;
}

bool AudioSystem::stop(uint64_t sourceId) {
    if (auto* s = findSource(sourceId)) {
        s->state = AudioPlaybackState::Stopped;
        s->playbackCursorFrames = 0.0;
        return true;
    }
    return false;
}

uint64_t AudioSystem::playOneShot(const AssetGUID& clipGuid, const glm::vec3& position, float volume, const std::string& bus) {
    AudioSource s{};
    s.clipGuid = clipGuid;
    s.position = position;
    s.volume = volume;
    s.busName = bus;
    s.state = AudioPlaybackState::Playing;
    s.oneShot = true;
    return createSource(s);
}

void AudioSystem::registerCue(const AudioCue& cue) {
    if (!cue.name.empty()) m_cues[cue.name] = cue;
}

uint64_t AudioSystem::triggerCue(const std::string& cueName, const glm::vec3& position) {
    auto it = m_cues.find(cueName);
    if (it == m_cues.end() || it->second.clipVariants.empty()) return 0;
    const auto& cue = it->second;
    const AssetGUID clip = cue.clipVariants[m_cueCounter++ % cue.clipVariants.size()];
    const float vol = 0.5f * (cue.volumeMin + cue.volumeMax);
    const uint64_t id = playOneShot(clip, position, vol, cue.busName);
    if (auto* s = findSource(id)) {
        s->pitch = 0.5f * (cue.pitchMin + cue.pitchMax);
        s->priority = cue.priority;
    }
    return id;
}

void AudioSystem::update(float /*dt*/, const PhysicsWorld* physicsWorld) {
    if (!m_initialized || !m_settings.enabled) return;

    // Optional line-of-sight occlusion via PhysicsWorld public query API
    if (m_settings.occlusionEnabled && physicsWorld) {
        for (auto& [id, s] : m_sources) {
            if (s.state != AudioPlaybackState::Playing || !s.spatialized) continue;
            const glm::vec3 toSrc = s.position - m_listener.position;
            const float dist = glm::length(toSrc);
            if (dist > 0.25f) {
                const RaycastHit hit = physicsWorld->raycast(m_listener.position, toSrc / dist, dist - 0.1f);
                s.occlusionFactor = hit.hit ? 0.65f : 0.0f;
            } else {
                s.occlusionFactor = 0.0f;
            }
        }
    }
}

void AudioSystem::mixFrames(float* outInterleavedStereo, uint32_t frameCount) {
    if (!outInterleavedStereo || frameCount == 0) return;
    const auto t0 = std::chrono::steady_clock::now();
    std::memset(outInterleavedStereo, 0, frameCount * 2u * sizeof(float));
    if (!m_initialized || !m_settings.enabled) return;

    // Gather playing sources and enforce maxActiveVoices by priority
    std::vector<AudioSource*> playing;
    playing.reserve(m_sources.size());
    for (auto& [id, s] : m_sources) {
        if (s.state == AudioPlaybackState::Playing && !s.muted) playing.push_back(&s);
    }
    std::sort(playing.begin(), playing.end(), [](const AudioSource* a, const AudioSource* b) {
        return a->priority > b->priority;
    });

    const uint32_t activeCount = std::min(static_cast<uint32_t>(playing.size()), m_settings.maxActiveVoices);
    m_stats.activeVoices = activeCount;
    m_stats.virtualizedVoices = static_cast<uint32_t>(playing.size()) - activeCount;

    const glm::vec3 listenerRight = glm::normalize(glm::cross(m_listener.forward, m_listener.up));
    std::vector<uint64_t> finishedOneShots;

    for (uint32_t v = 0; v < activeCount; ++v) {
        AudioSource* s = playing[v];
        const AudioClip* clip = findClip(s->clipGuid);
        if (!clip || clip->frameCount() == 0) continue;

        const float busGain = computeEffectiveBusGain(s->busName);
        float atten = 1.0f;
        float gainL = 0.7071f, gainR = 0.7071f;
        float doppler = 1.0f;

        if (s->spatialized && m_settings.spatialAudioEnabled) {
            const float dist = glm::length(s->position - m_listener.position);
            atten = evalDistanceAttenuation(dist, s->minDistance, s->maxDistance, s->rolloff, s->attenuation);
            evalStereoPanGains(m_listener.position, listenerRight, s->position, gainL, gainR);
            if (m_settings.dopplerEnabled) {
                doppler = evalDopplerPitch(m_listener.position, m_listener.velocity, s->position, s->velocity,
                                           m_settings.speedOfSound, m_settings.dopplerScale);
            }
        }

        const float occGain = 1.0f - s->occlusionFactor * 0.6f;
        const float finalL = s->volume * busGain * atten * occGain * gainL;
        const float finalR = s->volume * busGain * atten * occGain * gainR;
        const double step = static_cast<double>(s->pitch * doppler) *
                            (static_cast<double>(clip->sampleRate) / static_cast<double>(m_settings.sampleRate));
        const uint64_t totalFrames = clip->frameCount();

        for (uint32_t f = 0; f < frameCount; ++f) {
            if (s->playbackCursorFrames >= static_cast<double>(totalFrames)) {
                if (s->looping) {
                    s->playbackCursorFrames = std::fmod(s->playbackCursorFrames, static_cast<double>(totalFrames));
                } else {
                    s->state = AudioPlaybackState::Stopped;
                    if (s->oneShot) finishedOneShots.push_back(s->id);
                    break;
                }
            }
            const uint64_t idx = static_cast<uint64_t>(s->playbackCursorFrames);
            const float sample = clip->samples[idx * clip->channels];
            outInterleavedStereo[f * 2u]     += sample * finalL;
            outInterleavedStereo[f * 2u + 1] += sample * finalR;
            s->playbackCursorFrames += step;
        }
    }

    for (uint64_t id : finishedOneShots) m_sources.erase(id);

    // Soft-clip stereo output to [-1, 1]
    for (uint32_t i = 0; i < frameCount * 2u; ++i) {
        outInterleavedStereo[i] = std::clamp(outInterleavedStereo[i], -1.0f, 1.0f);
    }

    m_stats.lastMixCpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace sidera
