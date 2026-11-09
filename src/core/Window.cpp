#include "core/Window.h"
#include "core/Log.h"
#include "renderer/vulkan/VulkanCheck.h"

#include <stdexcept>

namespace sidera {

namespace {
void glfwErrorCallback(int errorCode, const char* description) {
    SIDERA_LOG_ERROR("GLFW", "Error ({}): {}", errorCode, description ? description : "Unknown");
}
} // namespace

Window::Window(const WindowConfig& config)
    : m_config(config) {
    glfwSetErrorCallback(glfwErrorCallback);

    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("Failed to initialize GLFW.");
    }

    if (glfwVulkanSupported() != GLFW_TRUE) {
        glfwTerminate();
        throw std::runtime_error("GLFW reports that Vulkan is not supported on this system.");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, m_config.resizable ? GLFW_TRUE : GLFW_FALSE);

    m_window = glfwCreateWindow(
        static_cast<int>(m_config.width),
        static_cast<int>(m_config.height),
        m_config.title.c_str(),
        nullptr,
        nullptr
    );

    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window.");
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
    glfwSetKeyCallback(m_window, keyCallback);

    const auto fbSize = getFramebufferSize();
    SIDERA_LOG_INFO("Window", "Created window '{}' ({}x{} px)",
                    m_config.title, fbSize.x, fbSize.y);
}

Window::~Window() {
    if (m_window) {
        SIDERA_LOG_INFO("Window", "Destroying GLFW window...");
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
    SIDERA_LOG_INFO("Window", "GLFW terminated cleanly.");
}

bool Window::shouldClose() const noexcept {
    return glfwWindowShouldClose(m_window) == GLFW_TRUE;
}

void Window::requestClose() noexcept {
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Window::pollEvents() const noexcept {
    glfwPollEvents();
}

void Window::waitEvents() const noexcept {
    glfwWaitEventsTimeout(0.05);
}

glm::uvec2 Window::getFramebufferSize() const noexcept {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

void Window::setSize(uint32_t width, uint32_t height) noexcept {
    glfwSetWindowSize(m_window, static_cast<int>(width), static_cast<int>(height));
}

void Window::minimize() noexcept {
    glfwIconifyWindow(m_window);
}

void Window::restore() noexcept {
    glfwRestoreWindow(m_window);
}

bool Window::isMinimized() const noexcept {
    const auto fbSize = getFramebufferSize();
    return fbSize.x == 0 || fbSize.y == 0;
}

std::vector<const char*> Window::getRequiredInstanceExtensions() const {
    uint32_t count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    if (!extensions || count == 0) {
        throw std::runtime_error("Failed to query required Vulkan instance extensions from GLFW.");
    }
    return std::vector<const char*>(extensions, extensions + count);
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VK_CHECK(
        glfwCreateWindowSurface(instance, m_window, nullptr, &surface),
        "Failed to create Vulkan window surface"
    );
    return surface;
}

void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->m_framebufferResized = true;
        self->m_config.width = static_cast<uint32_t>(width);
        self->m_config.height = static_cast<uint32_t>(height);
    }
}

void Window::keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

} // namespace sidera
