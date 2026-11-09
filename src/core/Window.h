#pragma once

#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sidera {

struct WindowConfig {
    uint32_t width = 1280;
    uint32_t height = 720;
    std::string title = "SIDERA Engine";
    bool resizable = true;
};

class Window {
public:
    explicit Window(const WindowConfig& config = {});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    [[nodiscard]] bool shouldClose() const noexcept;
    void requestClose() noexcept;
    void pollEvents() const noexcept;
    void waitEvents() const noexcept;

    [[nodiscard]] glm::uvec2 getFramebufferSize() const noexcept;
    void setSize(uint32_t width, uint32_t height) noexcept;
    void minimize() noexcept;
    void restore() noexcept;
    [[nodiscard]] bool isMinimized() const noexcept;
    [[nodiscard]] bool wasResized() const noexcept { return m_framebufferResized; }
    void resetResizedFlag() noexcept { m_framebufferResized = false; }

    // Input queries for 3D camera control
    [[nodiscard]] bool isKeyPressed(int key) const noexcept;
    [[nodiscard]] bool isMouseButtonPressed(int button) const noexcept;
    [[nodiscard]] glm::vec2 getCursorPosition() const noexcept;
    void setCursorCaptured(bool captured) noexcept;
    [[nodiscard]] bool isCursorCaptured() const noexcept { return m_cursorCaptured; }

    [[nodiscard]] std::vector<const char*> getRequiredInstanceExtensions() const;
    [[nodiscard]] VkSurfaceKHR createSurface(VkInstance instance) const;

    [[nodiscard]] GLFWwindow* getNativeHandle() const noexcept { return m_window; }

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    WindowConfig m_config;
    GLFWwindow* m_window = nullptr;
    bool m_framebufferResized = false;
    bool m_cursorCaptured = false;
};

} // namespace sidera
