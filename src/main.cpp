#include "core/Application.h"
#include "core/Log.h"

#include <cstdlib>
#include <exception>
#include <string_view>

int main(int argc, char* argv[]) {
    sidera::ApplicationConfig config{};

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--smoke-test") {
            config.smokeTest = true;
        } else if (arg == "--stats") {
            config.startWithStats = true;
        } else if (arg == "--stress-test" || arg == "--high-poly") {
            config.stressTest = true;
        } else if (arg == "--unlimited") {
            config.targetFps = 0.0f;
        } else if (arg == "--width" && i + 1 < argc) {
            config.window.width = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--height" && i + 1 < argc) {
            config.window.height = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--cam-pos" && i + 3 < argc) {
            config.cameraPos = glm::vec3(
                std::strtof(argv[i + 1], nullptr),
                std::strtof(argv[i + 2], nullptr),
                std::strtof(argv[i + 3], nullptr)
            );
            i += 3;
        } else if (arg == "--cam-rot" && i + 2 < argc) {
            config.cameraYaw = std::strtof(argv[i + 1], nullptr);
            config.cameraPitch = std::strtof(argv[i + 2], nullptr);
            i += 2;
        }
    }

    try {
        SIDERA_LOG_INFO("Core", "Starting SIDERA Engine v0.11.0...");
        {
            sidera::Application app(config);
            app.run();
        }
        SIDERA_LOG_INFO("Core", "SIDERA Engine terminated with EXIT_SUCCESS.");
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        SIDERA_LOG_ERROR("Core", "Fatal exception: {}", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        SIDERA_LOG_ERROR("Core", "Fatal unknown exception occurred.");
        return EXIT_FAILURE;
    }
}
