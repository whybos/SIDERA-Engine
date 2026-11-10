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
        } else if (arg == "--shadows" && i + 1 < argc) {
            const std::string_view val(argv[++i]);
            config.shadowsEnabled = (val == "on" || val == "true" || val == "1");
        } else if (arg == "--no-shadows") {
            config.shadowsEnabled = false;
        } else if (arg == "--shadow-resolution" && i + 1 < argc) {
            config.shadowResolution = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if ((arg == "--cascade-count" || arg == "--cascades") && i + 1 < argc) {
            config.cascadeCount = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--cascade-lambda" && i + 1 < argc) {
            config.cascadeLambda = std::strtof(argv[++i], nullptr);
        } else if (arg == "--show-cascades") {
            config.showCascades = true;
        } else if (arg == "--show-shadow-map") {
            config.showShadowMap = true;
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
        } else if (arg == "--renderer" && i + 1 < argc) {
            const std::string_view mode(argv[++i]);
            if (mode == "cpu" || mode == "CPU") {
                config.renderMode = sidera::RenderMode::Cpu;
            } else {
                config.renderMode = sidera::RenderMode::Gpu;
            }
        } else if (arg == "--gpu-driven" || arg == "--gpu") {
            config.renderMode = sidera::RenderMode::Gpu;
        } else if (arg == "--cpu-driven" || arg == "--cpu") {
            config.renderMode = sidera::RenderMode::Cpu;
        } else if (arg == "--frustum-culling") {
            config.frustumCulling = true;
        } else if (arg == "--no-frustum-culling") {
            config.frustumCulling = false;
        } else if (arg == "--occlusion-culling") {
            config.occlusionCulling = true;
        } else if (arg == "--no-occlusion-culling") {
            config.occlusionCulling = false;
        } else if (arg == "--show-hiz") {
            config.showHiZ = true;
        }
    }

    try {
        SIDERA_LOG_INFO("Core", "Starting SIDERA Engine v0.15.0...");
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
