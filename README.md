# SIDERA Engine 1.0 (`1.0.0-rc1`)

**SIDERA** is a modular, C++20, CMake-based **Vulkan 1.3** 3D game engine organized into four cleanly separated layers:

1. **SIDERA Engine Core** (`src/core`, `src/renderer`, `src/scene`, `src/assets`, `src/physics`, `src/audio`, `src/runtime`, `src/memory`, `src/streaming`, `src/profiling`)
2. **Reusable Game Frameworks** (`src/framework`) — optional RPG/Adventure systems (Character/Camera, Dialogue/Narrative, Quests, NPCs/Companions, Inventory/Equipment/Attributes/Progression, optional Combat, World Time/Events)
3. **Integrated Editor & Tools** (`src/editor`) — decoupled Editor Core, project management, command/undo-redo, and content editor services
4. **Individual Game Projects** (`examples/rpg_reference_project`) — completely decoupled from Engine Core

---

## Supported Platforms
- **Windows 10/11 x64** — Visual Studio 2022 (MSVC C++20), Vulkan 1.3 GPU (**Verified** on NVIDIA GeForce RTX 4060 Laptop GPU, Vulkan 1.4.341)
- Linux / macOS — *Not yet verified*

---

## Quick Start (Windows PowerShell)
```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake -S . -B build
& $cmake --build build --config Release --target sidera sidera_editor test_engine_1_0_release test_rpg_framework -j

# Run Runtime (Forward+ smoke test)
Push-Location .\build\Release
.\sidera.exe --smoke-test --render-path forward_plus
Pop-Location

# Run Editor with Reference Project
.\build\Release\sidera_editor.exe --open-project .\examples\rpg_reference_project

# Run Release & Framework Validation Tests
.\build\Release\test_engine_1_0_release.exe
.\build\Release\test_rpg_framework.exe
```

---

## Documentation
- **Build, Run, Package & GitHub Publish Guide**: [docs/guides/SIDERA_BUILD_RUN_AND_PUBLISH_GUIDE.md](docs/guides/SIDERA_BUILD_RUN_AND_PUBLISH_GUIDE.md)
- **Release Notes (`1.0.0-rc1`)**: [docs/releases/SIDERA_ENGINE_1_0_RC1.md](docs/releases/SIDERA_ENGINE_1_0_RC1.md)
- **Complete M01–M42 Milestone Audit**: [docs/quality/SIDERA_M01_M42_COMPLETE_MILESTONE_AUDIT.md](docs/quality/SIDERA_M01_M42_COMPLETE_MILESTONE_AUDIT.md)
- **Third-Party Licenses**: [docs/THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md)
- **Milestone 42 Report**: [docs/milestones/milestone_42_sidera_engine_1_0.md](docs/milestones/milestone_42_sidera_engine_1_0.md)

---

## Known Limitations (`1.0.0-rc1`)
- `sidera_editor.exe` currently runs its Editor Core & Content Editor services in CLI/headless mode; interactive Dear ImGui Vulkan viewport/docking GUI rendering is pending.
- `UISystem` generates DPI-scaled `UIDrawCommand` batches; Vulkan 2D quad/font rasterization in `Renderer` is pending.
- Only Windows x64 (MSVC 2022) has been built and verified.

---

## License
Third-party dependency licenses are documented in [docs/THIRD_PARTY_LICENSES.md](docs/THIRD_PARTY_LICENSES.md).
