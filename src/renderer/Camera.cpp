#include "renderer/Camera.h"
#include "core/Window.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace sidera {

Camera::Camera(const CameraConfig& config)
    : m_position(config.position)
    , m_yawDegrees(config.yawDegrees)
    , m_pitchDegrees(std::clamp(config.pitchDegrees, -config.maxPitchDegrees, config.maxPitchDegrees))
    , m_fovDegrees(config.fovDegrees)
    , m_nearPlane(config.nearPlane)
    , m_farPlane(config.farPlane)
    , m_aspectRatio(config.aspectRatio > 0.0f ? config.aspectRatio : (16.0f / 9.0f))
    , m_movementSpeed(config.movementSpeed)
    , m_mouseSensitivity(config.mouseSensitivity)
    , m_maxPitchDegrees(config.maxPitchDegrees) {
    updateVectorsAndView();
    updateProjection();
}

void Camera::processInput(Window& window, float deltaTime) noexcept {
    const float velocity = m_movementSpeed * deltaTime;

    if (window.isKeyPressed(GLFW_KEY_W)) {
        moveForward(velocity);
    }
    if (window.isKeyPressed(GLFW_KEY_S)) {
        moveForward(-velocity);
    }
    if (window.isKeyPressed(GLFW_KEY_D)) {
        moveRight(velocity);
    }
    if (window.isKeyPressed(GLFW_KEY_A)) {
        moveRight(-velocity);
    }
    if (window.isKeyPressed(GLFW_KEY_E) || window.isKeyPressed(GLFW_KEY_SPACE)) {
        moveUp(velocity);
    }
    if (window.isKeyPressed(GLFW_KEY_Q) || window.isKeyPressed(GLFW_KEY_LEFT_CONTROL)) {
        moveUp(-velocity);
    }

    // Mouse look is active while Right Mouse Button is held
    if (window.isMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT)) {
        window.setCursorCaptured(true);
        const glm::vec2 currentPos = window.getCursorPosition();

        if (m_firstMouseSample) {
            m_lastMousePos = currentPos;
            m_firstMouseSample = false;
        }

        const float xOffset = (currentPos.x - m_lastMousePos.x) * m_mouseSensitivity;
        const float yOffset = (m_lastMousePos.y - currentPos.y) * m_mouseSensitivity;
        m_lastMousePos = currentPos;

        if (xOffset != 0.0f || yOffset != 0.0f) {
            rotate(xOffset, yOffset);
        }
    } else {
        window.setCursorCaptured(false);
        m_firstMouseSample = true;
    }
}

void Camera::moveForward(float distance) noexcept {
    m_position += m_forward * distance;
    updateVectorsAndView();
}

void Camera::moveRight(float distance) noexcept {
    m_position += m_right * distance;
    updateVectorsAndView();
}

void Camera::moveUp(float distance) noexcept {
    m_position += m_worldUp * distance;
    updateVectorsAndView();
}

void Camera::rotate(float deltaYawDegrees, float deltaPitchDegrees) noexcept {
    m_yawDegrees += deltaYawDegrees;
    m_pitchDegrees = std::clamp(
        m_pitchDegrees + deltaPitchDegrees,
        -m_maxPitchDegrees,
        m_maxPitchDegrees
    );
    updateVectorsAndView();
}

void Camera::setPosition(const glm::vec3& position) noexcept {
    m_position = position;
    updateVectorsAndView();
}

void Camera::setOrientation(float yawDegrees, float pitchDegrees) noexcept {
    m_yawDegrees = yawDegrees;
    m_pitchDegrees = std::clamp(pitchDegrees, -m_maxPitchDegrees, m_maxPitchDegrees);
    updateVectorsAndView();
}

void Camera::setAspectRatio(float aspectRatio) noexcept {
    if (aspectRatio > 0.0001f) {
        m_aspectRatio = aspectRatio;
        updateProjection();
    }
}

void Camera::setViewportSize(uint32_t width, uint32_t height) noexcept {
    if (width > 0 && height > 0) {
        setAspectRatio(static_cast<float>(width) / static_cast<float>(height));
    }
}

void Camera::setFovDegrees(float fovDegrees) noexcept {
    m_fovDegrees = std::clamp(fovDegrees, 10.0f, 150.0f);
    updateProjection();
}

void Camera::setClipPlanes(float nearPlane, float farPlane) noexcept {
    if (nearPlane > 0.0001f && farPlane > nearPlane) {
        m_nearPlane = nearPlane;
        m_farPlane = farPlane;
        updateProjection();
    }
}

CameraUniformData Camera::getUniformData() const noexcept {
    CameraUniformData ubo{};
    ubo.view = m_viewMatrix;
    ubo.proj = m_projMatrix;
    ubo.viewProj = m_projMatrix * m_viewMatrix;
    return ubo;
}

void Camera::updateVectorsAndView() noexcept {
    const float yawRad = glm::radians(m_yawDegrees);
    const float pitchRad = glm::radians(m_pitchDegrees);

    glm::vec3 front{};
    front.x = std::cos(yawRad) * std::cos(pitchRad);
    front.y = std::sin(pitchRad);
    front.z = std::sin(yawRad) * std::cos(pitchRad);

    m_forward = glm::normalize(front);
    m_right = glm::normalize(glm::cross(m_forward, m_worldUp));
    m_up = glm::normalize(glm::cross(m_right, m_forward));

    m_viewMatrix = glm::lookAt(m_position, m_position + m_forward, m_up);
}

void Camera::updateProjection() noexcept {
    m_projMatrix = glm::perspective(
        glm::radians(m_fovDegrees),
        m_aspectRatio,
        m_nearPlane,
        m_farPlane
    );
    // Invert Y axis for Vulkan clip space (where -Y is top and +Y is bottom)
    m_projMatrix[1][1] *= -1.0f;
}

} // namespace sidera
