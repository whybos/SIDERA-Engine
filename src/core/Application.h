#pragma once

#include "core/FrameStats.h"
#include "core/Window.h"
#include "renderer/Renderer.h"
#include "renderer/vulkan/VulkanContext.h"

#include <glm/vec4.hpp>

#include <string>

namespace sidera {

struct ApplicationConfig {
    WindowConfig window{
        .width = 1280,
        .height = 720,
        .title = "SIDERA Engine v0.9.0 - PBR Foundation",
        .resizable = true
    };
    glm::vec4 clearColor{0.04f, 0.08f, 0.16f, 1.0f};
    bool smokeTest = false;
    bool startWithStats = false;   // --stats: begin with the F3 overlay enabled
    float targetFps = 60.0f;       // frame limiter target (<= 0 disables); F4 toggles at runtime
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
