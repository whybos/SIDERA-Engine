# SIDERA Engine 1.0.0 — End-User Acceptance Test Plan & Results

All items below are evaluated strictly against real execution on **Windows 10/11 x64 + MSVC 2022 + NVIDIA GeForce RTX 4060 Laptop GPU (Vulkan 1.4.341)**.

| Area | End-User Workflow | Verification Method | Status | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Installation** | Fresh install to clean dir (`Install-SIDERA.ps1`) | Executed from unpacked `SIDERA-1.0.0-win64.zip` | **PASS** | Verified 37 `.spv` shaders, `vulkan-1.dll`, Runtime, Editor, sample project |
| **Installation** | Uninstall preserves user projects (`Uninstall-SIDERA.ps1`) | Verified external user project remains intact after uninstall | **PASS** | Cleanly removes install dir & shortcuts only |
| **Editor** | Graphical GLFW + Vulkan + Dear ImGui Docking startup | `sidera_editor.exe --gui-smoke --open-project examples/rpg_reference_project --screenshot ...` | **PASS** | Real Vulkan + ImGui docking window rendered & screenshot captured |
| **Editor** | Welcome/Project Browser, Hierarchy, Inspector, Gizmos, Undo/Redo, Material/Lighting, Scene Save | Interactive `EditorGuiApp.cpp` wired to `EditorApplication` + `test_engine_1_0_release` | **PASS** | All controls backed by `EditorApplication` & `EditorCommandManager` |
| **Runtime** | Forward / Deferred / Forward+ GPU rendering + live Vulkan 2D Runtime HUD | `sidera.exe --smoke-test --render-path <path>` | **PASS** | Live Vulkan 2D `UIRenderer` (`ui_quad.*.spv`) + zero ImGui in Runtime |
| **CLI** | `--help`, `--version`, invalid argument rejection | Tested on `sidera.exe` and `sidera_editor.exe` | **PASS** | Valid flags exit `0`; invalid args exit `2` with clear error |
| **Framework** | Character/Camera, Dialogue, Quest, Inventory, Equipment, NPC/Companion, World Time, Save/Reload | `test_rpg_framework.exe` (Release & Debug) | **PASS** | End-to-end vertical slice verified |
| **Game Distribution** | Build standalone game (`Build-SideraGame.ps1`) & launch independently outside repo | Packaged `examples/rpg_reference_project` to temp dir & ran `RunGame.ps1 -SmokeTest` | **PASS** | Zero Editor/build files included; launches cleanly standalone |
| **Cross-Platform** | Linux / macOS builds & installers | Not executed | **NOT TESTED** | Windows x64 is the sole verified 1.0.0 shipping platform |
