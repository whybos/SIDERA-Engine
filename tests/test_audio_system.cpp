#include "audio/AudioSystem.h"
#include "physics/PhysicsWorld.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace sidera;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { ++g_passed; } \
        else { ++g_failed; std::cerr << "[FAIL] " << msg << "\n"; } \
    } while (0)

static void writeTestWAV(const std::filesystem::path& path, uint32_t sampleRate, uint32_t numSamples) {
    const uint32_t dataBytes = numSamples * 2u;
    const uint32_t riffSize = 36u + dataBytes;
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    f.write(reinterpret_cast<const char*>(&riffSize), 4);
    f.write("WAVEfmt ", 8);
    uint32_t fmtSize = 16; uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
    uint32_t byteRate = sampleRate * 2;
    f.write(reinterpret_cast<const char*>(&fmtSize), 4);
    f.write(reinterpret_cast<const char*>(&pcm), 2);
    f.write(reinterpret_cast<const char*>(&ch), 2);
    f.write(reinterpret_cast<const char*>(&sampleRate), 4);
    f.write(reinterpret_cast<const char*>(&byteRate), 4);
    f.write(reinterpret_cast<const char*>(&align), 2);
    f.write(reinterpret_cast<const char*>(&bits), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char*>(&dataBytes), 4);
    for (uint32_t i = 0; i < numSamples; ++i) {
        const int16_t s = static_cast<int16_t>(16000.0 * std::sin(2.0 * 3.14159265 * 440.0 * i / sampleRate));
        f.write(reinterpret_cast<const char*>(&s), 2);
    }
}

static void testWAVDecodingAndSpatialMath() {
    const std::filesystem::path wavPath = "audio_unit_test_tone.wav";
    writeTestWAV(wavPath, 48000, 1024);

    AudioClip clip{};
    clip.guid = AssetGUID::generate();
    CHECK(AudioSystem::decodeWAVFile(wavPath, clip), "16-bit PCM WAV decoded cleanly");
    CHECK(clip.sampleRate == 48000u && clip.channels == 1u && clip.frameCount() == 1024u, "Decoded WAV metadata matches");
    std::error_code ec;
    std::filesystem::remove(wavPath, ec);

    // Distance attenuation & Doppler & Stereo Pan
    const float aNear = evalDistanceAttenuation(1.0f, 1.0f, 50.0f, 1.0f, AttenuationModel::Inverse);
    const float aFar  = evalDistanceAttenuation(25.0f, 1.0f, 50.0f, 1.0f, AttenuationModel::Inverse);
    CHECK(aNear > aFar && aFar > 0.0f, "Inverse distance attenuation decreases with distance");

    const float doppApproach = evalDopplerPitch({0,0,0}, {0,0,0}, {10,0,0}, {-20,0,0}, 343.0f, 1.0f);
    const float doppRecede   = evalDopplerPitch({0,0,0}, {0,0,0}, {10,0,0}, {20,0,0}, 343.0f, 1.0f);
    CHECK(doppApproach > 1.0f && doppRecede < 1.0f, "Doppler raises pitch approaching and lowers receding");

    float gL = 0.0f, gR = 0.0f;
    evalStereoPanGains({0,0,0}, {1,0,0}, {10,0,0}, gL, gR);
    CHECK(gR > gL, "Source to the right pans stronger into right channel");
}

static void testMixerVoicesOcclusionAndCues() {
    AudioSystem audio;
    AudioSettings settings{};
    settings.maxActiveVoices = 2u;
    CHECK(audio.init(settings), "AudioSystem::init succeeds");
    CHECK(!audio.createBus("Master", "Master"), "Self-routing bus cycle rejected");
    CHECK(audio.createBus("AmbienceSub", "Ambient"), "Custom child bus created");
    CHECK(audio.setBusVolume("Ambient", 0.5f), "Parent bus volume updated");
    CHECK(std::abs(audio.computeEffectiveBusGain("AmbienceSub") - 0.5f) < 1e-4f, "Effective gain inherits parent bus");

    AudioClip clip{};
    clip.guid = AssetGUID::generate();
    clip.sampleRate = 48000;
    clip.channels = 1;
    clip.samples.assign(512, 0.5f);
    CHECK(audio.registerClip(clip), "AudioClip registered");

    // Physics occlusion test: wall between listener (0,1,0) and source (0,1,-10)
    PhysicsWorld phys;
    RigidBodyDesc wall{};
    wall.entityId = 50;
    wall.type = RigidBodyType::Static;
    wall.position = glm::vec3(0.0f, 1.0f, -5.0f);
    wall.collider.shape = ColliderShapeType::Box;
    wall.collider.halfExtents = glm::vec3(3.0f, 3.0f, 0.5f);
    phys.addBody(wall);

    audio.setListener({1, {0,1,0}, {0,0,-1}, {0,1,0}, {0,0,0}, true, 0});
    const uint64_t s1 = audio.playOneShot(clip.guid, {0, 1, -10}, 1.0f, "SFX");
    audio.playOneShot(clip.guid, {2, 1, 0}, 1.0f, "SFX");
    audio.playOneShot(clip.guid, {-2, 1, 0}, 1.0f, "SFX"); // exceeds maxActiveVoices=2 -> virtualized

    audio.update(1.0f / 60.0f, &phys);
    CHECK(audio.findSource(s1)->occlusionFactor > 0.0f, "PhysicsWorld raycast detects audio occlusion behind wall");

    std::vector<float> mixBuf(128 * 2, 0.0f);
    audio.mixFrames(mixBuf.data(), 128);
    CHECK(audio.getStats().activeVoices == 2u && audio.getStats().virtualizedVoices == 1u,
          "Voice manager enforces maxActiveVoices=2 and virtualizes 1 voice");
    CHECK(std::abs(mixBuf[0]) > 0.0f || std::abs(mixBuf[1]) > 0.0f, "Mixer produces non-silent stereo PCM output");

    // Reverb zone & Cue
    audio.addReverbZone({0, {0,1,0}, 15.0f, 5.0f, 0.6f, 1, true});
    CHECK(audio.evaluateListenerReverbWet() > 0.5f, "Listener inside reverb zone receives wet reverb mix");
    audio.registerCue({"Footstep", {clip.guid}, "SFX", 0.8f, 1.0f, 0.9f, 1.1f, 1});
    CHECK(audio.triggerCue("Footstep", {0, 1, -1}) != 0u, "AudioCue triggers playback");

    audio.shutdown();
    CHECK(!audio.isInitialized(), "AudioSystem::shutdown cleans up cleanly");
}

int main() {
    testWAVDecodingAndSpatialMath();
    testMixerVoicesOcclusionAndCues();

    std::cout << "test_audio_system: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
