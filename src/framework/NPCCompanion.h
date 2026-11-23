#pragma once

#include "scene/SceneTypes.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace sidera::framework {

enum class NPCState : uint8_t {
    Idle = 0,
    Patrol,
    Schedule,
    Interact,
    Dialogue
};

struct NPCScheduleEntry {
    float startHour = 0.0f;
    float endHour   = 24.0f;
    std::string activityId;
    glm::vec3 targetPosition{0.0f};
};

// Generic reusable NPC state machine with world-time schedule evaluation.
class NPCController {
public:
    explicit NPCController(EntityID id = kInvalidEntityID) : m_entityId(id) {}

    void setPosition(const glm::vec3& pos) noexcept { m_position = pos; }
    [[nodiscard]] const glm::vec3& getPosition() const noexcept { return m_position; }
    [[nodiscard]] EntityID getEntityId() const noexcept { return m_entityId; }
    [[nodiscard]] NPCState getState() const noexcept { return m_state; }
    [[nodiscard]] const std::string& getActiveActivity() const noexcept { return m_activeActivity; }

    void addScheduleEntry(NPCScheduleEntry entry) { m_schedule.push_back(std::move(entry)); }

    void beginDialogue() noexcept { m_state = NPCState::Dialogue; }
    void endDialogue() noexcept { m_state = NPCState::Idle; }
    void setState(NPCState s) noexcept { m_state = s; }

    void update(float worldHour, float dt, float moveSpeed = 2.0f) {
        if (m_state == NPCState::Dialogue || m_state == NPCState::Interact) return;

        for (const auto& entry : m_schedule) {
            if (worldHour >= entry.startHour && worldHour < entry.endHour) {
                m_state = NPCState::Schedule;
                m_activeActivity = entry.activityId;
                const glm::vec3 delta = entry.targetPosition - m_position;
                const float dist = glm::length(delta);
                if (dist > 0.05f) {
                    const float step = std::min(dist, moveSpeed * std::max(dt, 0.0f));
                    m_position += (delta / dist) * step;
                }
                return;
            }
        }
        m_state = NPCState::Idle;
        m_activeActivity.clear();
    }

private:
    EntityID m_entityId = kInvalidEntityID;
    glm::vec3 m_position{0.0f};
    NPCState m_state = NPCState::Idle;
    std::string m_activeActivity;
    std::vector<NPCScheduleEntry> m_schedule;
};

enum class CompanionCommand : uint8_t {
    Follow = 0,
    Wait,
    ReturnToPlayer
};

// Optional reusable companion controller.
class CompanionController {
public:
    explicit CompanionController(EntityID id = kInvalidEntityID) : m_entityId(id) {}

    void setPosition(const glm::vec3& pos) noexcept { m_position = pos; }
    [[nodiscard]] const glm::vec3& getPosition() const noexcept { return m_position; }
    [[nodiscard]] EntityID getEntityId() const noexcept { return m_entityId; }
    [[nodiscard]] CompanionCommand getCommand() const noexcept { return m_command; }

    void setCommand(CompanionCommand cmd) noexcept { m_command = cmd; }

    void update(const glm::vec3& leaderPos, float dt, float followDistance = 2.5f, float moveSpeed = 4.5f) {
        if (m_command == CompanionCommand::Wait) return;

        const glm::vec3 delta = leaderPos - m_position;
        const float dist = glm::length(delta);
        const float stopDist = (m_command == CompanionCommand::ReturnToPlayer) ? 1.0f : followDistance;

        if (dist > stopDist) {
            const float step = std::min(dist - stopDist, moveSpeed * std::max(dt, 0.0f));
            m_position += (delta / dist) * step;
        } else if (m_command == CompanionCommand::ReturnToPlayer) {
            m_command = CompanionCommand::Follow;
        }
    }

private:
    EntityID m_entityId = kInvalidEntityID;
    glm::vec3 m_position{0.0f};
    CompanionCommand m_command = CompanionCommand::Follow;
};

} // namespace sidera::framework
