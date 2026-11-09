#pragma once

#include "core/Window.h"
#include "renderer/Renderer.h"
#include "renderer/vulkan/VulkanContext.h"

#include <glm/vec4.hpp>

namespace sidera {

struct ApplicationConfig {
    WindowConfig window{
        .width = 1280,
        .height = 720,
        .title = "SIDERA Engine v0.5.0 - 3D Camera System",
        .resizable = true
    };
    glm::vec4 clearColor{0.04f, 0.08f, 0.16f, 1.0f};
    bool smokeTest = false;
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
};

} // namespace sidera
