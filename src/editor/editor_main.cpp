#include "core/Version.h"
#include "editor/EditorApplication.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char* argv[]) {
    bool headlessSmoke = false;
    std::string openProjectPath;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--headless-smoke") {
            headlessSmoke = true;
        } else if (arg == "--version") {
            std::cout << sidera::kSideraEngineBanner << " (Editor)\n";
            return 0;
        } else if (arg == "--open-project" && i + 1 < argc) {
            openProjectPath = argv[++i];
        }
    }

    sidera::editor::EditorApplication app;
    if (!app.init()) {
        std::cerr << "[SIDERA Editor] Failed to initialize EditorApplication.\n";
        return 1;
    }

    if (!openProjectPath.empty()) {
        if (!app.openProject(openProjectPath)) {
            std::cerr << "[SIDERA Editor] Failed to open project: " << openProjectPath << "\n";
            app.shutdown();
            return 1;
        }
        std::cout << "[SIDERA Editor] Opened project: " << openProjectPath << "\n";
    }

    app.tick(1.0f / 60.0f);
    std::cout << "[" << sidera::kSideraEngineBanner << " Editor] Initialized with "
              << app.getPanels().getPanelCount() << " core dockable panels"
              << (headlessSmoke ? " (headless smoke mode)." : ".") << "\n";

    app.shutdown();
    return 0;
}
