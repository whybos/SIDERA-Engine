#pragma once

#include "editor/EditorApplication.h"

#include <string>

namespace sidera::editor {

struct EditorGuiLaunchOptions {
    std::string openProjectPath;
    bool guiSmoke = false;
    std::string screenshotPath;
    uint32_t smokeFrames = 12;
};

// Launches the real interactive GLFW + Vulkan + Dear ImGui Docking SIDERA Editor.
// Returns 0 on clean exit, nonzero on initialization or runtime failure.
int runInteractiveEditorGui(const EditorGuiLaunchOptions& options);

} // namespace sidera::editor
