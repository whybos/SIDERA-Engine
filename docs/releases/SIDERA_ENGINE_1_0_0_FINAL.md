# SIDERA Engine 1.0.0 — Final Production Release Notes

**Release Version**: `SIDERA Engine 1.0.0` (superseding `1.0.0-rc1`)  
**Verified Platform**: Windows 10/11 x64, Visual Studio 2022 (MSVC C++20), Vulkan 1.3  
**Verified GPU**: NVIDIA GeForce RTX 4060 Laptop GPU (Vulkan 1.4.341)

---

## What Changed from `1.0.0-rc1` to `1.0.0` Final
1. **Interactive Graphical SIDERA Editor (`sidera_editor.exe`)**:
   - Implemented real **GLFW + Vulkan + Dear ImGui (`docking` branch)** graphical Editor (`src/editor/EditorGuiApp.cpp`), strictly isolated from Runtime (`sidera.exe` has zero Dear ImGui dependency).
   - Dockable workspace with Welcome/Project Browser, Main Menu Bar, Interactive 3D Scene Viewport (orbit/zoom, 3D grid, shaded entities, picking, XYZ gizmos, snapping), Hierarchy (create/duplicate/delete/search/multi-select), Inspector (live Transform/Material/Component editing with Undo/Redo), Asset Browser, Material & Lighting Editor, World/Terrain/Animation/Audio/RPG Framework tools, and Console & Profiler.
   - Real graphical screenshot capture verified via `--gui-smoke --screenshot`.
2. **Vulkan 2D Runtime UI Rendering (`src/renderer/UIRenderer.cpp`)**:
   - Implemented real Vulkan 1.3 2D UI pipeline (`shaders/ui_quad.vert`, `shaders/ui_quad.frag`) with alpha blending, scissor clipping, and embedded 5×7 ASCII bitmap font atlas.
   - Renders `sidera::runtime::UISystem::buildDrawList()` directly in `Renderer::recordCommandBuffer()` (panels, text, buttons, checkboxes, sliders, progress bars, input fields, scroll views) with zero Editor/ImGui dependency.
   - Live Reference Game HUD displayed in `sidera.exe`.
3. **Standalone Game Build Pipeline (`scripts/Build-SideraGame.ps1`)**:
   - Packages an independent distributable game folder (`sidera.exe`, `vulkan-1.dll`, 37 compiled `.spv` shaders, game project data, and `RunGame.ps1`) with zero Editor binaries or build intermediates — verified by launching outside the source repository.
4. **Strict CLI Validation**:
   - Both `sidera.exe` and `sidera_editor.exe` support `--help` and `--version`, validate arguments strictly, and return exit code `2` on invalid flags/values.
5. **End-User Installer & Uninstaller**:
   - Updated `scripts/Install-SIDERA.ps1` and `scripts/Uninstall-SIDERA.ps1` for `SIDERA Engine 1.0.0` with Start Menu / Desktop shortcut support and preservation of user projects outside the install directory.

## Remaining Documented Scope Notes
- **Platform Matrix**: Windows 10/11 x64 (MSVC 2022 + Vulkan 1.3) is built and verified; Linux/macOS remain unverified.
- **Editor 3D Viewport**: Renders live interactive 3D scene projection, grid, lit shaded entity proxies, picking, and gizmos inside the docked ImGui Vulkan viewport, while full PBR/shadow/terrain/volumetric rendering executes in the full Vulkan `sidera.exe` Runtime.
- **Third-Party Update**: Dear ImGui (`docking` branch, MIT license) added strictly as an Editor-only dependency (documented in `docs/THIRD_PARTY_LICENSES.md`).
