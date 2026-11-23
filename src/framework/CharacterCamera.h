#pragma once

#include "physics/PhysicsWorld.h"
#include "runtime/AnimationRuntime.h"
#include "runtime/InputManager.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace sidera::framework {

enum class MovementMode : uint8_t {
    Idle = 0,
    Walk,
    Run,
    Sprint,
    Crouch,
    Jump,
    Fall
};

struct CharacterMovementConfig {
    float walkSpeed   = 2.5f;
    float runSpeed    = 5.5f;
    float sprintSpeed = 8.5f;
    float crouchSpeed = 1.6f;
    float jumpSpeed   = 6.5f;
    float gravity     = -18.0f;
};

// Reusable third-person character controller integrating M37 CharacterController,
// M40 InputManager, and M40 AnimationStateMachine.
class ThirdPersonCharacter {
public:
    explicit ThirdPersonCharacter(CharacterMovementConfig cfg = {}) : m_config(cfg) {
        m_controller.movementSpeed = cfg.runSpeed;
        m_controller.jumpSpeed     = cfg.jumpSpeed;
        m_controller.gravity       = cfg.gravity;
    }

    void setPosition(const glm::vec3& pos) noexcept { m_controller.teleport(pos); }

    [[nodiscard]] const glm::vec3& getPosition() const noexcept { return m_controller.position; }
    [[nodiscard]] float getYawRadians() const noexcept { return m_yaw; }
    [[nodiscard]] MovementMode getMovementMode() const noexcept { return m_mode; }
    [[nodiscard]] float getCurrentSpeed() const noexcept { return m_currentSpeed; }
    [[nodiscard]] bool isGrounded() const noexcept { return m_controller.isGrounded(); }
    [[nodiscard]] sidera::CharacterController& getPhysicsController() noexcept { return m_controller; }

    void update(const sidera::runtime::InputManager& input,
                const sidera::PhysicsWorld& physicsWorld,
                sidera::runtime::AnimationStateMachine* animSm,
                float cameraYawRadians,
                float dt) {
        const float moveZ = (input.isActionHeld("MoveForward") ? 1.0f : 0.0f) -
                            (input.isActionHeld("MoveBackward") ? 1.0f : 0.0f);
        const float moveX = (input.isActionHeld("MoveRight") ? 1.0f : 0.0f) -
                            (input.isActionHeld("MoveLeft") ? 1.0f : 0.0f);
        const bool sprint = input.isActionHeld("Sprint");
        const bool crouch = input.isActionHeld("Crouch");
        const bool jump   = input.isActionHeld("Jump");

        glm::vec2 inputDir(moveX, moveZ);
        const float inputLen = glm::length(inputDir);
        glm::vec3 worldWish(0.0f);

        if (inputLen > 0.01f) {
            inputDir /= std::max(inputLen, 1.0f);
            const float sinY = std::sin(cameraYawRadians);
            const float cosY = std::cos(cameraYawRadians);
            const glm::vec3 forward(sinY, 0.0f, cosY);
            const glm::vec3 right(cosY, 0.0f, -sinY);
            worldWish = glm::normalize(forward * inputDir.y + right * inputDir.x);
            m_yaw = std::atan2(worldWish.x, worldWish.z);
        }

        float targetSpeed = 0.0f;
        if (inputLen > 0.01f) {
            if (crouch) {
                targetSpeed = m_config.crouchSpeed;
                m_mode = MovementMode::Crouch;
            } else if (sprint) {
                targetSpeed = m_config.sprintSpeed;
                m_mode = MovementMode::Sprint;
            } else {
                targetSpeed = m_config.runSpeed;
                m_mode = MovementMode::Run;
            }
        } else {
            m_mode = crouch ? MovementMode::Crouch : MovementMode::Idle;
        }

        m_controller.movementSpeed = targetSpeed;
        if (jump && m_controller.isGrounded()) {
            m_controller.jump();
            m_mode = MovementMode::Jump;
        }

        m_controller.move(physicsWorld, worldWish, dt);

        if (m_controller.isFalling()) {
            m_mode = MovementMode::Fall;
        }
        m_currentSpeed = (inputLen > 0.01f) ? targetSpeed : 0.0f;

        if (animSm) {
            animSm->setParameter("Speed", m_currentSpeed);
            animSm->setParameter("IsGrounded", m_controller.isGrounded() ? 1.0f : 0.0f);
            animSm->setParameter("IsCrouching", crouch ? 1.0f : 0.0f);
            animSm->update(dt);
        }
    }

private:
    CharacterMovementConfig m_config{};
    sidera::CharacterController m_controller{};
    MovementMode m_mode = MovementMode::Idle;
    float m_yaw = 0.0f;
    float m_currentSpeed = 0.0f;
};

struct ThirdPersonCameraConfig {
    float distance      = 5.0f;
    float minDistance   = 2.0f;
    float maxDistance   = 12.0f;
    float minPitchDeg   = -45.0f;
    float maxPitchDeg   = 65.0f;
    float followSmooth  = 10.0f;
    glm::vec3 pivotOffset{0.0f, 1.6f, 0.0f};
};

// Reusable third-person orbit/follow camera with pitch/yaw clamping and smoothing.
class ThirdPersonCamera {
public:
    explicit ThirdPersonCamera(ThirdPersonCameraConfig cfg = {}) : m_config(cfg) {}

    void addInput(float deltaYawDeg, float deltaPitchDeg, float deltaZoom = 0.0f) noexcept {
        m_yawDeg += deltaYawDeg;
        m_pitchDeg = std::clamp(m_pitchDeg + deltaPitchDeg, m_config.minPitchDeg, m_config.maxPitchDeg);
        m_config.distance = std::clamp(m_config.distance + deltaZoom, m_config.minDistance, m_config.maxDistance);
    }

    void update(const glm::vec3& targetPosition, float dt) noexcept {
        const glm::vec3 desiredPivot = targetPosition + m_config.pivotOffset;
        if (!m_initialized) {
            m_smoothedPivot = desiredPivot;
            m_initialized = true;
        } else {
            const float alpha = 1.0f - std::exp(-m_config.followSmooth * std::max(dt, 0.0f));
            m_smoothedPivot = glm::mix(m_smoothedPivot, desiredPivot, alpha);
        }

        const float yawRad   = glm::radians(m_yawDeg);
        const float pitchRad = glm::radians(m_pitchDeg);
        const glm::vec3 offset(
            std::sin(yawRad) * std::cos(pitchRad) * m_config.distance,
            std::sin(pitchRad) * m_config.distance,
            std::cos(yawRad) * std::cos(pitchRad) * m_config.distance);
        m_cameraPos = m_smoothedPivot - offset;
    }

    [[nodiscard]] glm::mat4 getViewMatrix() const noexcept {
        return glm::lookAt(m_cameraPos, m_smoothedPivot, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    [[nodiscard]] const glm::vec3& getPosition() const noexcept { return m_cameraPos; }
    [[nodiscard]] float getYawRadians() const noexcept { return glm::radians(m_yawDeg); }
    [[nodiscard]] float getPitchDegrees() const noexcept { return m_pitchDeg; }
    [[nodiscard]] float getDistance() const noexcept { return m_config.distance; }

private:
    ThirdPersonCameraConfig m_config{};
    float m_yawDeg = 0.0f;
    float m_pitchDeg = 15.0f;
    glm::vec3 m_smoothedPivot{0.0f};
    glm::vec3 m_cameraPos{0.0f, 2.0f, -5.0f};
    bool m_initialized = false;
};

} // namespace sidera::framework
