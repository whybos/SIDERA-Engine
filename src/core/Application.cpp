#include "core/Application.h"
#include "core/Log.h"

#include <chrono>
#include <format>
#include <string>
#include <thread>

namespace sidera {

namespace {
constexpr std::string_view kEngineVersion = "v0.17.0";
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
                 m_config.showCascades ? 1u : (m_config.showShadowMap ? 2u : 0u),
                 m_config.renderMode)
    , m_baseTitle(m_config.window.title) {
    m_renderer.setCascadeLambda(m_config.cascadeLambda);
    m_renderer.setFrustumCullingEnabled(m_config.frustumCulling);
    m_renderer.setOcclusionCullingEnabled(m_config.occlusionCulling);
    if (m_config.showHiZ) {
        m_renderer.setCullingDebugMode(1);
    }
    m_statsEnabled = m_config.startWithStats;
    m_limiterEnabled = m_config.targetFps > 0.0f;
    if (m_config.stressTest) {
        m_renderer.setStressTestMode(true);
    }
    if (m_config.instanceCount > 0) {
        if (m_config.instanceCount == 100) {
            m_renderer.setInstancingMode(Renderer::InstancingMode::Count100);
        } else if (m_config.instanceCount == 1000) {
            m_renderer.setInstancingMode(Renderer::InstancingMode::Count1000);
        } else if (m_config.instanceCount == 10000) {
            m_renderer.setInstancingMode(Renderer::InstancingMode::Count10000);
        } else {
            m_renderer.setInstancingMode(Renderer::InstancingMode::Custom, m_config.instanceCount);
        }
    }
    if (m_config.cameraPos) {
        m_renderer.getCamera().setPosition(*m_config.cameraPos);
    }
    if (m_config.cameraYaw && m_config.cameraPitch) {
        m_renderer.getCamera().setOrientation(*m_config.cameraYaw, *m_config.cameraPitch);
    }
    if (m_config.splitView) {
        m_renderer.toggleSplitView();
    }
    if (m_config.viewportMode != ViewportMode::Lit) {
        m_renderer.setViewportMode(m_config.viewportMode);
    }
    if (m_config.normalSpace != 0) {
        m_renderer.toggleNormalSpace();
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

    const bool isGpu = (m_renderer.getRenderMode() == RenderMode::Gpu);
    const RendererTimings timings = m_renderer.consumeTimings();
    const auto& cs = m_renderer.getLastCullingStats();

    std::string instancingModeStr = "NORMAL";
    switch (m_renderer.getInstancingMode()) {
        case Renderer::InstancingMode::Count100: instancingModeStr = "100"; break;
        case Renderer::InstancingMode::Count1000: instancingModeStr = "1K"; break;
        case Renderer::InstancingMode::Count10000: instancingModeStr = "10K"; break;
        case Renderer::InstancingMode::Custom: instancingModeStr = std::format("{}", m_renderer.getTotalInstanceCount()); break;
        default: break;
    }

    std::string drawsInfo;
    std::string cullingInfo;
    if (isGpu) {
        drawsInfo = std::format("{} instanced draws [{} inst, vis {}, culled {}(F:{},O:{})] in {} batches (+{} shd), {} tris",
                                m_renderer.getMainApiDrawCallCount(),
                                m_renderer.getTotalInstanceCount(),
                                cs.finalVisible,
                                cs.frustumCulled + cs.occlusionCulled,
                                cs.frustumCulled,
                                cs.occlusionCulled,
                                m_renderer.getInstanceBatchCount(),
                                m_renderer.getShadowApiDrawCallCount(),
                                m_renderer.getTriangleCount());
        cullingInfo = std::format(" | Culling: F:{} O:{}",
                                  m_renderer.isFrustumCullingEnabled() ? "ON" : "OFF",
                                  m_renderer.isOcclusionCullingEnabled() ? "ON" : "OFF");
        if (m_renderer.getCullingDebugMode() > 0) {
            cullingInfo += (m_renderer.getCullingDebugMode() == 1 ? " [Hi-Z FULL]" : " [Hi-Z PIP]");
        }
    } else {
        drawsInfo = std::format("{} draws (+{} shadow), {} tris",
                                m_renderer.getMainApiDrawCallCount(),
                                m_renderer.getShadowDrawCallCount(),
                                m_renderer.getTriangleCount());
    }

    std::string timingStr;
    if (timings.cpuSamples > 0) {
        timingStr = std::format(" | CPU-sub: {:.2f} ms", timings.cpuSubmitAvgMs);
    }
    if (timings.gpuSamples > 0) {
        if (isGpu) {
            timingStr += std::format(" | GPU: {:.2f} ms (gen {:.2f}, shd {:.2f}, main {:.2f})",
                                     timings.gpuTotalAvgMs,
                                     timings.gpuGenAvgMs,
                                     timings.gpuShadowAvgMs,
                                     timings.gpuMainAvgMs);
        } else {
            timingStr += std::format(" | GPU: {:.2f} ms (shd {:.2f}, main {:.2f})",
                                     timings.gpuTotalAvgMs,
                                     timings.gpuShadowAvgMs,
                                     timings.gpuMainAvgMs);
        }
    }

    const auto& vpMgr = m_renderer.getViewportManager();
    const auto& activeVp = vpMgr.getActiveViewport();
    const auto texStats = m_renderer.getTextureManager().getStats();

    const std::string vpInfo = std::format(
        " | VIEWPORT: {} ({}{}) | TEXTURES: {} (mips {}, {:.2f} MB, {} hits)",
        getViewportModeName(activeVp.mode),
        getViewportLayoutName(vpMgr.getLayout()),
        activeVp.mode == ViewportMode::Normals ? (activeVp.normalSpace == 0 ? ", WORLD" : ", VIEW") : "",
        texStats.totalTextures,
        texStats.totalMipLevels,
        static_cast<double>(texStats.gpuMemoryUsageBytes) / (1024.0 * 1024.0),
        texStats.cacheHits
    );

    const std::string title = std::format(
        "SIDERA {} [{}] | {:.1f} FPS (target {}) | {:.2f} ms | 1% low {:.1f} | "
        "{}x{} {} | {}{}{}{} | limiter {}{}{}{}",
        kEngineVersion,
        isGpu ? "GPU-INSTANCING" : "CPU-DRIVEN",
        r.fps,
        m_limiterEnabled ? std::format("{:.0f}", m_config.targetFps) : std::string("off"),
        r.avgMs,
        r.onePercentLowFps,
        extent.width, extent.height, presentMode,
        drawsInfo,
        cullingInfo,
        timingStr,
        m_renderer.isShadowsEnabled() ? std::format(" | {}", shadowInfo) : "",
        m_limiterEnabled ? "ON" : "OFF",
        m_renderer.isStressTestMode() ? " | STRESS MODE" : "",
        std::format(" | INSTANCING: {}", instancingModeStr),
        vpInfo
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
    SIDERA_LOG_INFO("Core", "================================================================================");
    SIDERA_LOG_INFO("Core", "SIDERA Engine v0.17.0 — Runtime Controls Guide:");
    SIDERA_LOG_INFO("Core", "  [Movement]         W/A/S/D = Move, E/Q = Elevate/Descend, Right-Click + Drag = Look");
    SIDERA_LOG_INFO("Core", "  [Diagnostics]      F3 = Diagnostics HUD Overlay (FPS, GPU/CPU Times, Viewport & Textures)");
    SIDERA_LOG_INFO("Core", "  [Performance]      F4 = 60 FPS Limiter, F5 = High-Poly Stress Test");
    SIDERA_LOG_INFO("Core", "  [Shadows]          F6 = Shadows ON/OFF, F7 = Shadow Debug Overlay");
    SIDERA_LOG_INFO("Core", "  [Rendering Mode]   F8 = Toggle GPU-Driven Indirect / CPU Direct Submission");
    SIDERA_LOG_INFO("Core", "  [Culling System]   F9 = Toggle Frustum Culling, F10 = Toggle Occlusion Culling, F11 = Hi-Z Depth View");
    SIDERA_LOG_INFO("Core", "  [GPU Instancing]   F12 = Cycle Instancing (Normal, 100, 1,000, 10,000 instances)");
    SIDERA_LOG_INFO("Core", "  [Viewport Modes]   V = Cycle Viewport Rendering Mode (14 modes)");
    SIDERA_LOG_INFO("Core", "  [Normal Space]     B = Toggle Normal Space (World Space / View Space)");
    SIDERA_LOG_INFO("Core", "  [Viewport Layout]  T = Toggle Viewport Layout (Single / Split Horizontal Dual)");
    SIDERA_LOG_INFO("Core", "  [Exit]             ESC = Exit Application");
    SIDERA_LOG_INFO("Core", "================================================================================");
    SIDERA_LOG_INFO(
        "Core",
        "Entering frame loop (WASD/EQ move, Hold Right-Click look, F3 stats, F4 limiter, F5 stress, F6 shadows, F7 shadow dbg, F8 GPU/CPU mode, F9 Frustum cull, F10 Occlusion cull, F11 Hi-Z dbg, F12 Instancing, V Viewport Mode, B Normal Space, T Split View, ESC exit)."
    );

    const auto startTime = std::chrono::steady_clock::now();
    auto lastFrameTime = startTime;

    bool testedMovement = false;
    bool testedMouseLookAndPitch = false;
    bool testedResizeStep1 = false;
    bool testedMinimize = false;
    bool testedRestore = false;
    bool testedResizeStep2 = false;
    bool testedViewportModes = false;
    bool testedStressToggling = false;
    bool f3WasDown = false;
    bool f4WasDown = false;
    bool f5WasDown = false;
    bool f6WasDown = false;
    bool f7WasDown = false;
    bool f8WasDown = false;
    bool f9WasDown = false;
    bool f10WasDown = false;
    bool f11WasDown = false;
    bool f12WasDown = false;
    bool vWasDown = false;
    bool bWasDown = false;
    bool tWasDown = false;

    while (!m_window.shouldClose()) {
        const auto currentTime = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        m_window.pollEvents();

        // F3: stats, F4: limiter, F5: stress, F6: shadows, F7: shadow dbg, F8: GPU/CPU mode, F9: frustum culling, F10: occlusion culling, F11: Hi-Z dbg, F12: Instancing, V: Viewport mode, B: Normal space, T: Split view
        const bool f3Down = m_window.isKeyPressed(GLFW_KEY_F3);
        const bool f4Down = m_window.isKeyPressed(GLFW_KEY_F4);
        const bool f5Down = m_window.isKeyPressed(GLFW_KEY_F5);
        const bool f6Down = m_window.isKeyPressed(GLFW_KEY_F6);
        const bool f7Down = m_window.isKeyPressed(GLFW_KEY_F7);
        const bool f8Down = m_window.isKeyPressed(GLFW_KEY_F8);
        const bool f9Down = m_window.isKeyPressed(GLFW_KEY_F9);
        const bool f10Down = m_window.isKeyPressed(GLFW_KEY_F10);
        const bool f11Down = m_window.isKeyPressed(GLFW_KEY_F11);
        const bool f12Down = m_window.isKeyPressed(GLFW_KEY_F12);
        const bool vDown = m_window.isKeyPressed(GLFW_KEY_V);
        const bool bDown = m_window.isKeyPressed(GLFW_KEY_B);
        const bool tDown = m_window.isKeyPressed(GLFW_KEY_T);

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
        if (f8Down && !f8WasDown) {
            m_renderer.toggleRenderMode();
            m_frameStats.reset();
            SIDERA_LOG_INFO("Core", "Rendering submission switched: {}.",
                            m_renderer.getRenderMode() == RenderMode::Gpu ? "GPU-DRIVEN (indirect)" : "CPU-DRIVEN (direct)");
        }
        if (f9Down && !f9WasDown) {
            m_renderer.toggleFrustumCulling();
            m_frameStats.reset();
        }
        if (f10Down && !f10WasDown) {
            m_renderer.toggleOcclusionCulling();
            m_frameStats.reset();
        }
        if (f11Down && !f11WasDown) {
            m_renderer.cycleCullingDebugMode();
            m_frameStats.reset();
        }
        if (f12Down && !f12WasDown) {
            m_renderer.cycleInstancingMode();
            m_frameStats.reset();
        }
        if (vDown && !vWasDown) {
            m_renderer.cycleViewportMode();
            m_frameStats.reset();
        }
        if (bDown && !bWasDown) {
            m_renderer.toggleNormalSpace();
            m_frameStats.reset();
        }
        if (tDown && !tWasDown) {
            m_renderer.toggleSplitView();
            m_frameStats.reset();
        }
        f3WasDown = f3Down;
        f4WasDown = f4Down;
        f5WasDown = f5Down;
        f6WasDown = f6Down;
        f7WasDown = f7Down;
        f8WasDown = f8Down;
        f9WasDown = f9Down;
        f10WasDown = f10Down;
        f11WasDown = f11Down;
        f12WasDown = f12Down;
        vWasDown = vDown;
        bWasDown = bDown;
        tWasDown = tDown;

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
            } else if (!testedViewportModes && elapsed >= std::chrono::milliseconds(2050)) {
                testedViewportModes = true;
                SIDERA_LOG_INFO("Core", "[SmokeTest] Cycling viewport rendering modes & split view...");
                m_renderer.cycleViewportMode();
                m_renderer.toggleNormalSpace();
                m_renderer.toggleSplitView();
                m_renderer.toggleSplitView();
                m_renderer.setViewportMode(ViewportMode::Lit);
            } else if (!testedStressToggling && elapsed >= std::chrono::milliseconds(2200)) {
                testedStressToggling = true;
                SIDERA_LOG_INFO("Core", "[SmokeTest] Dynamically toggling stress test mode (Normal -> Stress -> Normal)...");
                m_renderer.toggleStressTestMode(); // ON
                m_renderer.toggleStressTestMode(); // OFF
                m_renderer.toggleStressTestMode(); // ON
                m_renderer.toggleStressTestMode(); // OFF
            } else if (elapsed >= std::chrono::milliseconds(2700)) {
                SIDERA_LOG_INFO("Core", "[SmokeTest] Frame loop verification complete; requesting close.");
                m_window.requestClose();
            }
        }
    }

    m_vulkanContext.waitIdle();
    SIDERA_LOG_INFO("Core", "Frame loop exited cleanly.");
}

} // namespace sidera
