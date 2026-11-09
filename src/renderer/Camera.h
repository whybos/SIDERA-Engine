#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace sidera {

class Window;

struct alignas(16) CameraUniformData {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::mat4 viewProj{1.0f};
};

struct CameraConfig {
    glm::vec3 position{1.65f, 1.20f, 2.35f};
    float yawDegrees = -125.0f;
    float pitchDegrees = -22.0f;
    float fovDegrees = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 200.0f;
    float aspectRatio = 16.0f / 9.0f;
    float movementSpeed = 3.5f;
    float mouseSensitivity = 0.12f;
    float maxPitchDegrees = 89.0f;
};

class Camera {
public:
    explicit Camera(const CameraConfig& config = {});

    void processInput(Window& window, float deltaTime) noexcept;

    void moveForward(float distance) noexcept;
    void moveRight(float distance) noexcept;
    void moveUp(float distance) noexcept;
    void rotate(float deltaYawDegrees, float deltaPitchDegrees) noexcept;

    void setPosition(const glm::vec3& position) noexcept;
    void setOrientation(float yawDegrees, float pitchDegrees) noexcept;
    void setAspectRatio(float aspectRatio) noexcept;
    void setViewportSize(uint32_t width, uint32_t height) noexcept;
    void setFovDegrees(float fovDegrees) noexcept;
    void setClipPlanes(float nearPlane, float farPlane) noexcept;
    void setMovementSpeed(float speed) noexcept { m_movementSpeed = speed; }
    void setMouseSensitivity(float sensitivity) noexcept { m_mouseSensitivity = sensitivity; }

    [[nodiscard]] const glm::vec3& getPosition() const noexcept { return m_position; }
    [[nodiscard]] const glm::vec3& getForward() const noexcept { return m_forward; }
    [[nodiscard]] const glm::vec3& getRight() const noexcept { return m_right; }
    [[nodiscard]] const glm::vec3& getUp() const noexcept { return m_up; }
    [[nodiscard]] float getYawDegrees() const noexcept { return m_yawDegrees; }
    [[nodiscard]] float getPitchDegrees() const noexcept { return m_pitchDegrees; }
    [[nodiscard]] float getFovDegrees() const noexcept { return m_fovDegrees; }
    [[nodiscard]] float getNearPlane() const noexcept { return m_nearPlane; }
    [[nodiscard]] float getFarPlane() const noexcept { return m_farPlane; }
    [[nodiscard]] float getAspectRatio() const noexcept { return m_aspectRatio; }
    [[nodiscard]] float getMovementSpeed() const noexcept { return m_movementSpeed; }
    [[nodiscard]] float getMouseSensitivity() const noexcept { return m_mouseSensitivity; }

    [[nodiscard]] const glm::mat4& getViewMatrix() const noexcept { return m_viewMatrix; }
    [[nodiscard]] const glm::mat4& getProjectionMatrix() const noexcept { return m_projMatrix; }
    [[nodiscard]] CameraUniformData getUniformData() const noexcept;

private:
    void updateVectorsAndView() noexcept;
    void updateProjection() noexcept;

    glm::vec3 m_position{1.65f, 1.20f, 2.35f};
    glm::vec3 m_forward{0.0f, 0.0f, -1.0f};
    glm::vec3 m_right{1.0f, 0.0f, 0.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
    glm::vec3 m_worldUp{0.0f, 1.0f, 0.0f};

    float m_yawDegrees = -125.0f;
    float m_pitchDegrees = -22.0f;
    float m_fovDegrees = 60.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 200.0f;
    float m_aspectRatio = 16.0f / 9.0f;

    float m_movementSpeed = 3.5f;
    float m_mouseSensitivity = 0.12f;
    float m_maxPitchDegrees = 89.0f;

    glm::mat4 m_viewMatrix{1.0f};
    glm::mat4 m_projMatrix{1.0f};

    bool m_firstMouseSample = true;
    glm::vec2 m_lastMousePos{0.0f, 0.0f};
};

} // namespace sidera
