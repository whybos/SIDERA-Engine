#pragma once

#include "core/FrameStats.h"
#include "core/Window.h"
#include "renderer/Renderer.h"
#include "renderer/Viewport.h"
#include "renderer/vulkan/VulkanContext.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <optional>
#include <string>

namespace sidera {

struct ApplicationConfig {
    WindowConfig window{
        .width = 1920,
        .height = 1080,
        .title = "SIDERA Engine v0.17.0 - Advanced Texture System & Viewport Modes",
        .resizable = true
    };
    glm::vec4 clearColor{0.04f, 0.08f, 0.16f, 1.0f};
    bool smokeTest = false;
    bool startWithStats = false;   // --stats: begin with the F3 overlay enabled
    float targetFps = 60.0f;       // frame limiter target (<= 0 disables); F4 toggles at runtime
    bool stressTest = false;       // --stress-test: high-poly stress scene (>= 1M triangles)
    bool shadowsEnabled = true;    // --shadows on/off: directional shadow mapping
    uint32_t shadowResolution = 2048; // --shadow-resolution 1024/2048/4096
    uint32_t cascadeCount = 4;     // --cascade-count 3/4
    float cascadeLambda = 0.75f;   // --cascade-lambda 0.0..1.0
    bool showCascades = false;     // --show-cascades: cascade split color visualization
    bool showShadowMap = false;    // --show-shadow-map: shadow depth map debug visualization
    RenderMode renderMode = RenderMode::Gpu; // --renderer gpu|cpu (GPU-driven indirect by default)
    bool frustumCulling = true;    // --frustum-culling / --no-frustum-culling
    bool occlusionCulling = true;  // --occlusion-culling / --no-occlusion-culling
    bool showHiZ = false;          // --show-hiz: Hi-Z depth map debug visualization
    uint32_t instanceCount = 0;    // --instances <N> (0 = normal, 1, 100, 1000, 10000)
    ViewportMode viewportMode = ViewportMode::Lit; // --viewport-mode <mode>
    bool splitView = false;        // --split-view: dual viewport split screen
    uint32_t normalSpace = 0;      // --normal-space <world|view> (0 = world, 1 = view)
    std::optional<glm::vec3> cameraPos;
    std::optional<float> cameraYaw;
    std::optional<float> cameraPitch;
};

class Application {
public:
    explicit Application(const ApplicationConfig& config = {});
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    void run();

private:
    ApplicationConfig m_config;
    Window m_window;
    VulkanContext m_vulkanContext;
    Renderer m_renderer;
    FrameStats m_frameStats;
    bool m_statsEnabled = false;
    bool m_limiterEnabled = true;
    std::string m_baseTitle;

    void updateStatsTitle(const FrameStatsReport& report);
};

} // namespace sidera
