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
    SIDERA_LOG_INFO("Core", "Entering frame loop (press ESC or close window to exit).");

    const auto startTime = std::chrono::steady_clock::now();
    bool testedResizeStep1 = false;
    bool testedMinimize = false;
    bool testedRestore = false;
    bool testedResizeStep2 = false;

    while (!m_window.shouldClose()) {
        m_window.pollEvents();
        m_renderer.drawFrame(m_window);

        if (m_config.smokeTest) {
            const auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (!testedResizeStep1 && elapsed >= std::chrono::milliseconds(500)) {
                testedResizeStep1 = true;
                SIDERA_LOG_INFO("Core", "[SmokeTest] Resizing window to 1024x576...");
                m_window.setSize(1024, 576);
            } else if (!testedMinimize && elapsed >= std::chrono::milliseconds(900)) {
                testedMinimize = true;
                SIDERA_LOG_INFO("Core", "[SmokeTest] Minimizing window...");
                m_window.minimize();
            } else if (!testedRestore && elapsed >= std::chrono::milliseconds(1300)) {
                testedRestore = true;
                SIDERA_LOG_INFO("Core", "[SmokeTest] Restoring window...");
                m_window.restore();
            } else if (!testedResizeStep2 && elapsed >= std::chrono::milliseconds(1700)) {
                testedResizeStep2 = true;
                SIDERA_LOG_INFO("Core", "[SmokeTest] Resizing window back to 1280x720...");
                m_window.setSize(1280, 720);
            } else if (elapsed >= std::chrono::milliseconds(2200)) {
                SIDERA_LOG_INFO("Core", "[SmokeTest] Frame loop verification complete; requesting close.");
                m_window.requestClose();
            }
        }
    }

    m_vulkanContext.waitIdle();
    SIDERA_LOG_INFO("Core", "Frame loop exited cleanly.");
}

} // namespace sidera
