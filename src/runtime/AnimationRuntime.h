#pragma once

#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera::runtime {

struct AnimationEvent {
    float triggerTimeSec = 0.0f;
    std::string eventName;
};

struct AnimationClip {
    std::string name;
    float durationSec = 1.0f;
    bool looping = true;
    glm::vec3 startPos{0.0f};
    glm::vec3 endPos{0.0f};
    std::vector<AnimationEvent> events;
};

struct AnimationTransition {
    std::string fromState;
    std::string toState;
    std::string paramName;
    float threshold = 0.5f;
    float crossfadeSec = 0.2f;
};

// Reusable Animation Runtime: Playback, Linear Crossfade Blending, State Machine & Timed Events.
class AnimationStateMachine {
public:
    void addState(const AnimationClip& clip) {
        m_states[clip.name] = clip;
        if (m_currentState.empty()) m_currentState = clip.name;
    }

    void addTransition(const AnimationTransition& t) { m_transitions.push_back(t); }
    void setParameter(const std::string& name, float value) { m_params[name] = value; }
    void setEventCallback(std::function<void(const std::string&)> cb) { m_onEvent = std::move(cb); }

    void update(float dt) {
        if (m_currentState.empty() || dt <= 0.0f) return;

        // Check state transitions
        for (const auto& t : m_transitions) {
            if (t.fromState == m_currentState && m_states.count(t.toState) > 0) {
                auto pit = m_params.find(t.paramName);
                if (pit != m_params.end() && pit->second >= t.threshold) {
                    m_prevState = m_currentState;
                    m_currentState = t.toState;
                    m_timeSec = 0.0f;
                    m_blendDuration = std::max(t.crossfadeSec, 0.001f);
                    m_blendElapsed = 0.0f;
                    break;
                }
            }
        }

        const auto& clip = m_states[m_currentState];
        const float prevTime = m_timeSec;
        m_timeSec += dt * m_playbackSpeed;
        if (m_blendElapsed < m_blendDuration) {
            m_blendElapsed = std::min(m_blendElapsed + dt, m_blendDuration);
        }

        // Fire timed animation events in (prevTime, m_timeSec]
        for (const auto& ev : clip.events) {
            if (ev.triggerTimeSec > prevTime && ev.triggerTimeSec <= m_timeSec) {
                if (m_onEvent) m_onEvent(ev.eventName);
            }
        }

        if (clip.durationSec > 0.0f && m_timeSec >= clip.durationSec) {
            m_timeSec = clip.looping ? std::fmod(m_timeSec, clip.durationSec) : clip.durationSec;
        }
    }

    [[nodiscard]] glm::vec3 evaluateBlendedPosition() const noexcept {
        auto it = m_states.find(m_currentState);
        if (it == m_states.end()) return glm::vec3(0.0f);
        const auto& curr = it->second;
        const float t = (curr.durationSec > 0.0f) ? std::clamp(m_timeSec / curr.durationSec, 0.0f, 1.0f) : 0.0f;
        const glm::vec3 currPos = curr.startPos + (curr.endPos - curr.startPos) * t;
        if (!m_prevState.empty() && m_blendElapsed < m_blendDuration && m_states.count(m_prevState) > 0) {
            const float alpha = std::clamp(m_blendElapsed / m_blendDuration, 0.0f, 1.0f);
            const glm::vec3 prevPos = m_states.at(m_prevState).endPos;
            return prevPos * (1.0f - alpha) + currPos * alpha;
        }
        return currPos;
    }

    [[nodiscard]] const std::string& getCurrentState() const noexcept { return m_currentState; }
    [[nodiscard]] float getCurrentTime() const noexcept { return m_timeSec; }
    void setPlaybackSpeed(float s) noexcept { m_playbackSpeed = std::clamp(s, 0.0f, 4.0f); }

private:
    std::unordered_map<std::string, AnimationClip> m_states;
    std::vector<AnimationTransition> m_transitions;
    std::unordered_map<std::string, float> m_params;
    std::string m_currentState;
    std::string m_prevState;
    float m_timeSec = 0.0f;
    float m_playbackSpeed = 1.0f;
    float m_blendDuration = 0.0f;
    float m_blendElapsed = 0.0f;
    std::function<void(const std::string&)> m_onEvent;
};

} // namespace sidera::runtime
