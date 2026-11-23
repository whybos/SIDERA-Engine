# SIDERA Engine 1.0.0-rc1 — Release Notes

**Release Candidate**: `SIDERA Engine 1.0.0-rc1`  
**Platform**: Windows 10/11 (x64), MSVC 2022 (C++20), Vulkan 1.3  
**Verified GPU**: NVIDIA GeForce RTX 4060 Laptop GPU (Vulkan 1.4.341, 64-bit GPU timestamps)

---

## Highlights & Capabilities
1. **Engine Core & Vulkan 1.3 Rendering**:
   - Forward, Deferred, and Forward+ rendering paths (all 35 GLSL shaders compiled to SPIR-V).
   - PBR, IBL (procedural HDR sky, irradiance, prefiltered cubemap, BRDF LUT), Cascaded Shadow Maps, GPU-driven indirect rendering, frustum/Hi-Z occlusion culling, instancing, procedural terrain + quadtree LOD, vegetation/foliage, virtual texturing, HDR/Bloom/SSAO/Tonemapping, Volumetrics, SSGI, and SSR reflections.
   - M33 GPU timestamp profiling, M34 memory budgets & async streaming, M35 asset pipeline (`AssetDatabase`, `AssetGUID`), M36 scene & prefab serialization, M37 3D physics + `CharacterController` + `InteractionSystem`, M38 spatial audio, M40 input, animation runtime, `RuntimeEventBus`, `SaveSystem`, `LocalizationManager`, and `FormatMigrationRegistry`.
2. **Integrated Editor (`sidera_editor`)**:
   - M39.1/M39.2 modular Editor Core & Content Editor infrastructure (project creation/validation/opening, dockable panel registry, selection, undo/redo commands, hierarchy/inspector/asset/material/lighting/terrain/animation/audio controllers).
   - CLI support for `--version`, `--headless-smoke`, and `--open-project <dir>`.
3. **Reusable RPG / Adventure Framework (`src/framework/`)**:
   - Strictly layered on top of Engine Core (`SIDERA_BUILD_FRAMEWORKS=ON`): `FrameworkRegistry`, `ThirdPersonCharacter`, `ThirdPersonCamera`, `DialogueSystem`, `NarrativeState`, `QuestManager`, `NPCController`, `CompanionController`, `InventoryContainer`, `EquipmentManager`, `AttributeSet`, `ProgressionSystem`, optional `CombatSystem`, and `WorldTimeSystem`.
4. **Distribution & Packaging**:
   - CMake `install()` rules + CPack portable ZIP (`SIDERA-1.0.0-rc1-win64.zip`).
   - Windows PowerShell installer/uninstaller (`scripts/Install-SIDERA.ps1`, `scripts/Uninstall-SIDERA.ps1`).
   - Reusable sample project (`examples/rpg_reference_project/`).

## Versioning & Format Compatibility Policy
- Semantic versioning: `MAJOR.MINOR.PATCH[-PRERELEASE]` (`1.0.0-rc1`).
- Persisted formats (`.sideraproj`, `.siderameta`, `.siderascene`, `.sideraprefab`, `.siderasave`, `.sideraeditor`, `.sideralayout`, `frameworkdef`) use explicit `schema_version` fields managed via `FormatMigrationRegistry` (`src/runtime/FormatMigration.h`), supporting non-destructive forward migrations, unknown-field preservation, and safe rejection of unsupported future versions.

## Supported Platform Matrix
| Platform | Compiler | Graphics API | Status |
| :--- | :--- | :--- | :--- |
| Windows 10/11 x64 | MSVC 2022 (C++20) | Vulkan 1.3 | **Verified** |
| Linux / macOS | GCC / Clang | Vulkan 1.3 / MoltenVK | **Not Verified** (do not assume out-of-the-box support) |

## Known Limitations (Honest RC1 Blockers / Technical Debt)
1. **Interactive Dear ImGui Vulkan Editor GUI**: `sidera_editor.exe` currently executes the full Editor Core & Content Editor backend (project management, panel registry, commands, undo/redo, scene/asset APIs) in headless/CLI mode; interactive Dear ImGui Vulkan viewport & docking window rendering is not yet wired.
2. **Live Vulkan 2D Runtime UI Rasterizer**: `UISystem::buildDrawList()` generates DPI-scaled `UIDrawCommand` batches, but GPU 2D quad/font rasterization in `Renderer` is pending.
3. **CI Pipeline**: `.github/workflows/ci.yml` is provided for Windows MSVC builds/tests/packaging, but has not yet been executed on GitHub Actions because commits remain local until explicitly pushed.
4. **Cross-Platform Validation**: Only Windows x64 + MSVC 2022 + NVIDIA RTX 4060 Laptop GPU has been built and verified.
