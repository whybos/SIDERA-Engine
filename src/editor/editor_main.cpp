#include "editor/EditorApplication.h"

#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    bool headlessSmoke = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--headless-smoke") {
            headlessSmoke = true;
        }
    }

    sidera::editor::EditorApplication app;
    if (!app.init()) {
        std::cerr << "[SIDERA Editor] Failed to initialize EditorApplication.\n";
        return 1;
    }

    app.tick(1.0f / 60.0f);
    std::cout << "[SIDERA Editor v0.39.1] Initialized with "
              << app.getPanels().getPanelCount() << " core dockable panels"
              << (headlessSmoke ? " (headless smoke mode)." : ".") << "\n";

    app.shutdown();
    return 0;
}
