#include "core/Version.h"
#include "editor/EditorApplication.h"
#include "editor/EditorGuiApp.h"

#include <iostream>
#include <string>
#include <string_view>

namespace {
void printEditorUsage() {
    std::cout
        << sidera::kSideraEngineBanner << " — Graphical Editor\n"
        << "Usage: sidera_editor [options]\n"
        << "  --help                  Show this help message and exit\n"
        << "  --version               Print engine editor version and exit\n"
        << "  --open-project <dir>    Open an existing SIDERA project directory\n"
        << "  --gui-smoke             Run graphical Vulkan + Dear ImGui smoke session and exit\n"
        << "  --screenshot <path>     Capture BMP screenshot during --gui-smoke\n"
        << "  --headless-smoke        Run Editor Core services in headless smoke mode\n";
}
} // namespace

int main(int argc, char* argv[]) {
    bool headlessSmoke = false;
    sidera::editor::EditorGuiLaunchOptions guiOpts{};

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            printEditorUsage();
            return 0;
        } else if (arg == "--version") {
            std::cout << sidera::kSideraEngineBanner << " (Editor)\n";
            return 0;
        } else if (arg == "--headless-smoke") {
            headlessSmoke = true;
        } else if (arg == "--gui-smoke") {
            guiOpts.guiSmoke = true;
        } else if (arg == "--open-project") {
            if (i + 1 >= argc) {
                std::cerr << "[SIDERA Editor] Error: --open-project requires a directory path.\n";
                return 2;
            }
            guiOpts.openProjectPath = argv[++i];
        } else if (arg == "--screenshot") {
            if (i + 1 >= argc) {
                std::cerr << "[SIDERA Editor] Error: --screenshot requires an output file path.\n";
                return 2;
            }
            guiOpts.screenshotPath = argv[++i];
        } else {
            std::cerr << "[SIDERA Editor] Error: unknown argument '" << arg << "'. Use --help for usage.\n";
            return 2;
        }
    }

    if (headlessSmoke) {
        sidera::editor::EditorApplication app;
        if (!app.init()) {
            std::cerr << "[SIDERA Editor] Failed to initialize EditorApplication.\n";
            return 1;
        }
        if (!guiOpts.openProjectPath.empty()) {
            if (!app.openProject(guiOpts.openProjectPath)) {
                std::cerr << "[SIDERA Editor] Failed to open project: " << guiOpts.openProjectPath << "\n";
                app.shutdown();
                return 1;
            }
            std::cout << "[SIDERA Editor] Opened project: " << guiOpts.openProjectPath << "\n";
        }
        app.tick(1.0f / 60.0f);
        std::cout << "[" << sidera::kSideraEngineBanner << " Editor] Initialized with "
                  << app.getPanels().getPanelCount() << " core dockable panels (headless smoke mode).\n";
        app.shutdown();
        return 0;
    }

    return sidera::editor::runInteractiveEditorGui(guiOpts);
}
