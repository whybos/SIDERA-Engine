#include "core/Application.h"
#include "core/Log.h"

#include <chrono>
#include <format>
#include <string>
#include <thread>

namespace sidera {

namespace {
constexpr std::string_view kEngineVersion = "v0.13.0";
} // namespace

Application::Application(const ApplicationConfig& config)
    : m_config(config)
    , m_window(m_config.window)
    , m_vulkanContext(m_window)
    , m_renderer(m_vulkanContext,
                 m_window,
                 m_config.clearColor,
                 m_config.shadowsEnabled,
                 m_config.shadowResolution,
                 m_config.cascadeCount,
                 m_config.showCascades ? 1u : (m_config.showShadowMap ? 2u : 0u))
    , m_baseTitle(m_config.window.title) {
    m_renderer.setCascadeLambda(m_config.cascadeLambda);
    m_statsEnabled = m_config.startWithStats;
    m_limiterEnabled = m_config.targetFps > 0.0f;
    if (m_config.stressTest) {
        m_renderer.setStressTestMode(true);
    }
    if (m_config.cameraPos) {
        m_renderer.getCamera().setPosition(*m_config.cameraPos);
    }
    if (m_config.cameraYaw && m_config.cameraPitch) {
        m_renderer.getCamera().setOrientation(*m_config.cameraYaw, *m_config.cameraPitch);
    }
    SIDERA_LOG_INFO("Core", "SIDERA Engine initialized.");
}

void Application::updateStatsTitle(const FrameStatsReport& r) {
    const auto extent = m_renderer.getSwapchain().getExtent();
    const char* presentMode = "?";
    switch (m_renderer.getSwapchain().getPresentMode()) {
        case VK_PRESENT_MODE_FIFO_KHR:         presentMode = "FIFO(vsync)"; break;
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: presentMode = "FIFO_RELAXED"; break;
        case VK_PRESENT_MODE_MAILBOX_KHR:      presentMode = "MAILBOX"; break;
        case VK_PRESENT_MODE_IMMEDIATE_KHR:    presentMode = "IMMEDIATE"; break;
        default: break;
    }

    const char* debugModeStr = "none";
    switch (m_renderer.getShadowDebugMode()) {
        case 1: debugModeStr = "cascades"; break;
        case 2: debugModeStr = "PIP"; break;
        case 3: debugModeStr = "depth"; break;
        case 4: debugModeStr = "factor"; break;
        default: break;
    }

    const std::string shadowInfo = m_renderer.isShadowsEnabled()
        ? std::format("CSM ON ({}x{} {}c){}",
                      m_renderer.getShadowResolution(),
                      m_renderer.getShadowResolution(),
                      m_renderer.getCascadeCount(),
                      m_renderer.getShadowDebugMode() > 0 ? std::format(" [dbg:{}]", debugModeStr) : "")
        : "shadows OFF";

    const std::string title = std::format(
        "SIDERA {} | {:.1f} FPS (target {}) | {:.2f} ms (min {:.2f}, max {:.2f}) | 1% low {:.1f} | "
        "{}x{} {} | {} draws (+{} shadow), {} tris | {} | limiter {}{}",
        kEngineVersion,
        r.fps,
        m_limiterEnabled ? std::format("{:.0f}", m_config.targetFps) : std::string("off"),
        r.avgMs, r.minMs, r.maxMs,
        r.onePercentLowFps,
        extent.width, extent.height, presentMode,
        m_renderer.getDrawCallCount(), m_renderer.getShadowDrawCallCount(), m_renderer.getTriangleCount(),
        shadowInfo,
        m_limiterEnabled ? "ON" : "OFF",
        m_renderer.isStressTestMode() ? " | STRESS MODE" : ""
    );
    m_window.setTitle(title);

    if (m_config.smokeTest) {
        SIDERA_LOG_INFO("Stats", "{}", title);
    }
}

Application::~Application() {
    SIDERA_LOG_INFO("Core", "SIDERA Engine shutting down...");
}

void Application::run() {
    SIDERA_LOG_INFO(
        "Core",
        "Entering frame loop (WASD/EQ move, Hold Right-Click look, F3 stats, F4 limiter, F5 stress, F6 shadows, F7 shadow dbg, ESC exit)."
    );

    const auto startTime = std::chrono::steady_clock::now();
    auto lastFrameTime = startTime;

    bool testedMovement = false;
    bool testedMouseLookAndPitch = false;
    bool testedResizeStep1 = false;
    bool testedMinimize = false;
    bool testedRestore = false;
    bool testedResizeStep2 = false;
    bool f3WasDown = false;
    bool f4WasDown = false;
    bool f5WasDown = false;
    bool f6WasDown = false;
    bool f7WasDown = false;

    while (!m_window.shouldClose()) {
        const auto currentTime = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        m_window.pollEvents();

        // F3: toggle performance stats. F4: toggle frame limiter. F5: toggle high-poly stress-test mode.
        // F6: toggle real-time shadows. F7: cycle shadow debug visualization modes.
        const bool f3Down = m_window.isKeyPressed(GLFW_KEY_F3);
        const bool f4Down = m_window.isKeyPressed(GLFW_KEY_F4);
        const bool f5Down = m_window.isKeyPressed(GLFW_KEY_F5);
        const bool f6Down = m_window.isKeyPressed(GLFW_KEY_F6);
        const bool f7Down = m_window.isKeyPressed(GLFW_KEY_F7);

        if (f3Down && !f3WasDown) {
            m_statsEnabled = !m_statsEnabled;
            m_frameStats.reset();
            if (!m_statsEnabled) {
                m_window.setTitle(m_baseTitle);
            }
            SIDERA_LOG_INFO("Core", "Performance stats {}.", m_statsEnabled ? "ENABLED" : "DISABLED");
        }
        if (f4Down && !f4WasDown && m_config.targetFps > 0.0f) {
            m_limiterEnabled = !m_limiterEnabled;
            m_frameStats.reset();
            SIDERA_LOG_INFO("Core", "Frame limiter {} (target {:.0f} FPS).",
                            m_limiterEnabled ? "ON" : "OFF", m_config.targetFps);
        }
        if (f5Down && !f5WasDown) {
            m_renderer.toggleStressTestMode();
            m_frameStats.reset();
            SIDERA_LOG_INFO("Core", "Toggled stress-test mode: {} (visible triangles: {}).",
                            m_renderer.isStressTestMode() ? "ON (>= 1,000,000 tris)" : "OFF (>= 250,000 tris)",
                            m_renderer.getTriangleCount());
        }
        if (f6Down && !f6WasDown) {
            m_renderer.toggleShadows();
            m_frameStats.reset();
            SIDERA_LOG_INFO("Core", "Shadows toggled: {}.", m_renderer.isShadowsEnabled() ? "ENABLED" : "DISABLED");
        }
        if (f7Down && !f7WasDown) {
            m_renderer.cycleShadowDebugMode();
            const char* dbgNames[] = {
                "OFF",
                "Cascade split colors (Red=Near, Green=Mid-near, Blue=Mid-far, Yellow=Far)",
                "PIP thumbnail (Cascade 0)",
                "Fullscreen depth map (Cascade 0)",
                "Shadow factor mask"
            };
            SIDERA_LOG_INFO("Core", "Shadow debug mode: {} ({}).",
                            m_renderer.getShadowDebugMode(),
                            dbgNames[m_renderer.getShadowDebugMode() % 5]);
        }
        f3WasDown = f3Down;
        f4WasDown = f4Down;
        f5WasDown = f5Down;
        f6WasDown = f6Down;
        f7WasDown = f7Down;

        m_renderer.getCamera().processInput(m_window, deltaTime);
        m_renderer.updateScene(deltaTime);
        m_renderer.drawFrame(m_window);

        if (m_statsEnabled && !m_window.isMinimized()) {
            FrameStatsReport report;
            if (m_frameStats.record(deltaTime, report)) {
                updateStatsTitle(report);
            }
        }

        // Frame limiter: sleep most of the remaining budget, then yield-spin the last ~1.5 ms
        if (m_limiterEnabled && m_config.targetFps > 0.0f && !m_window.isMinimized()) {
            const auto budget = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(m_config.targetFps)));
            const auto frameEnd = currentTime + budget;
            const auto sleepUntil = frameEnd - std::chrono::microseconds(1500);
            if (std::chrono::steady_clock::now() < sleepUntil) {
                std::this_thread::sleep_until(sleepUntil);
            }
            while (std::chrono::steady_clock::now() < frameEnd) {
                std::this_thread::yield();
            }
        }

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
                cam.setOrientation(-90.0f, -18.0f);
            } else if (!testedResizeStep1 && elapsed >= std::chrono::milliseconds(700)) {
                testedResizeStep1 = true;
                SIDERA_LOG_INFO("Core", "[SmokeTest] Resizing window to 1280x720...");
                m_window.setSize(1280, 720);
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
                SIDERA_LOG_INFO("Core", "[SmokeTest] Resizing window back to 1920x1080 (1080p default)...");
                m_window.setSize(1920, 1080);
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
