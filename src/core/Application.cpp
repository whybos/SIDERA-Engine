#include "core/Application.h"
#include "core/Log.h"

#include <chrono>

namespace sidera {

Application::Application(const ApplicationConfig& config)
    : m_config(config)
    , m_window(m_config.window)
    , m_vulkanContext(m_window)
    , m_renderer(m_vulkanContext, m_window, m_config.clearColor) {
    SIDERA_LOG_INFO("Core", "SIDERA Engine initialized.");
}

Application::~Application() {
    SIDERA_LOG_INFO("Core", "SIDERA Engine shutting down...");
}

void Application::run() {
    SIDERA_LOG_INFO(
        "Core",
        "Entering frame loop (WASD/EQ to move, Hold Right-Click to look, ESC to exit)."
    );

    const auto startTime = std::chrono::steady_clock::now();
    auto lastFrameTime = startTime;

    bool testedMovement = false;
    bool testedMouseLookAndPitch = false;
    bool testedResizeStep1 = false;
    bool testedMinimize = false;
    bool testedRestore = false;
    bool testedResizeStep2 = false;

    while (!m_window.shouldClose()) {
        const auto currentTime = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        m_window.pollEvents();
        m_renderer.getCamera().processInput(m_window, deltaTime);
        m_renderer.drawFrame(m_window);

        if (m_config.smokeTest) {
            const auto elapsed = currentTime - startTime;
            Camera& cam = m_renderer.getCamera();

            if (!testedMovement && elapsed >= std::chrono::milliseconds(250)) {
                testedMovement = true;
                cam.moveForward(0.45f);
                cam.moveRight(-0.30f);
                const auto p1 = cam.getPosition();
                SIDERA_LOG_INFO(
                    "Camera",
                    "[SmokeTest] Forward/Left movement -> pos=[{:.2f}, {:.2f}, {:.2f}]",
                    p1.x, p1.y, p1.z
                );
                cam.moveRight(0.30f);
                cam.moveForward(-0.45f);
                const auto p2 = cam.getPosition();
                SIDERA_LOG_INFO(
                    "Camera",
                    "[SmokeTest] Backward/Right movement restored -> pos=[{:.2f}, {:.2f}, {:.2f}]",
                    p2.x, p2.y, p2.z
                );
            } else if (!testedMouseLookAndPitch && elapsed >= std::chrono::milliseconds(450)) {
                testedMouseLookAndPitch = true;
                cam.rotate(15.0f, 10.0f);
                SIDERA_LOG_INFO(
                    "Camera",
                    "[SmokeTest] Mouse look rotation -> yaw={:.1f} deg, pitch={:.1f} deg",
                    cam.getYawDegrees(), cam.getPitchDegrees()
                );
                cam.rotate(0.0f, 300.0f);
                const float maxClamped = cam.getPitchDegrees();
                cam.rotate(0.0f, -600.0f);
                const float minClamped = cam.getPitchDegrees();
                SIDERA_LOG_INFO(
                    "Camera",
                    "[SmokeTest] Pitch limit clamp verified: maxPitch={:.1f} deg, minPitch={:.1f} deg",
                    maxClamped, minClamped
                );
                cam.setOrientation(-125.0f, -22.0f);
            } else if (!testedResizeStep1 && elapsed >= std::chrono::milliseconds(700)) {
                testedResizeStep1 = true;
                SIDERA_LOG_INFO("Core", "[SmokeTest] Resizing window to 1024x576...");
                m_window.setSize(1024, 576);
            } else if (!testedMinimize && elapsed >= std::chrono::milliseconds(1050)) {
                testedMinimize = true;
                SIDERA_LOG_INFO("Core", "[SmokeTest] Minimizing window...");
                m_window.minimize();
            } else if (!testedRestore && elapsed >= std::chrono::milliseconds(1450)) {
                testedRestore = true;
                SIDERA_LOG_INFO("Core", "[SmokeTest] Restoring window...");
                m_window.restore();
            } else if (!testedResizeStep2 && elapsed >= std::chrono::milliseconds(1800)) {
                testedResizeStep2 = true;
                SIDERA_LOG_INFO("Core", "[SmokeTest] Resizing window back to 1280x720...");
                m_window.setSize(1280, 720);
            } else if (elapsed >= std::chrono::milliseconds(2250)) {
                SIDERA_LOG_INFO("Core", "[SmokeTest] Frame loop verification complete; requesting close.");
                m_window.requestClose();
            }
        }
    }

    m_vulkanContext.waitIdle();
    SIDERA_LOG_INFO("Core", "Frame loop exited cleanly.");
}

} // namespace sidera
