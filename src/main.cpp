#include "core/Application.h"
#include "core/Log.h"

#include <cstdlib>
#include <exception>
#include <string_view>

int main(int argc, char* argv[]) {
    sidera::ApplicationConfig config{};

    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--smoke-test") {
            config.smokeTest = true;
        }
    }

    try {
        SIDERA_LOG_INFO("Core", "Starting SIDERA Engine v0.5.0...");
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
