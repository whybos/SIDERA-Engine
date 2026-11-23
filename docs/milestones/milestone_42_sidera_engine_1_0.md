# Milestone 42 — SIDERA Engine 1.0 (`1.0.0-rc1`)

## 1. Release Overview
- **Engine Version**: `SIDERA Engine 1.0.0-rc1` (`src/core/Version.h`, `CMakeLists.txt` `VERSION 1.0.0`)
- **Base Commit (M41)**: `2019f69 feat(framework): Milestone 41 — Reusable RPG Adventure Framework`
- **Branch / Remote**: `master` → `origin https://github.com/whybos/SIDERA-Engine` (local commit only; not pushed)
- **Target Platform**: Windows 10/11 x64, Visual Studio 2022 (MSVC C++20), Vulkan 1.3 (verified on NVIDIA GeForce RTX 4060 Laptop GPU, Vulkan 1.4.341)

## 2. Implemented Deliverables
1. **Engine Version & Public API Stabilization**:
   - `src/core/Version.h` (`kSideraVersionMajor=1`, `kSideraVersionMinor=0`, `kSideraVersionPatch=0`, `kSideraVersionString="1.0.0-rc1"`, `kSideraEngineBanner="SIDERA Engine 1.0.0-rc1"`).
   - Runtime (`sidera.exe`) and Editor (`sidera_editor.exe`) report `SIDERA Engine 1.0.0-rc1` via `--version` and startup banners; Editor adds `--open-project <dir>`.
2. **Persisted Format Compatibility & Non-Destructive Migration**:
   - `src/runtime/FormatMigration.h` (`FormatMigrationRegistry`, `FormatCompatibility`, `VersionedDocument`) registering all 8 SIDERA 1.0 schemas (`sideraproj`, `siderameta`, `siderascene`, `sideraprefab`, `siderasave`, `sideraeditor`, `sideralayout`, `frameworkdef`), preserving unknown fields non-destructively and safely rejecting unsupported future versions.
3. **Modular CMake, Packaging & Installer**:
   - `CMakeLists.txt`: `SIDERA_BUILD_EDITOR`, `SIDERA_BUILD_FRAMEWORKS`, `SIDERA_BUILD_TESTS`, `SIDERA_BUILD_EXAMPLES`, clean `install()` rules, and CPack `ZIP` packaging (`build/packages/SIDERA-1.0.0-rc1-win64.zip`, `0.99 MB`).
   - Windows PowerShell installer/uninstaller: `scripts/Install-SIDERA.ps1` & `scripts/Uninstall-SIDERA.ps1`.
   - CI workflow template: `.github/workflows/ci.yml` (not yet executed on GitHub Actions because commits remain local).
4. **Reusable Sample Project**:
   - `examples/rpg_reference_project/` (`project.sideraproj`, `scenes/main.siderascene`, `data/quests.json`), completely decoupled from Engine Core and packaged in the distribution.
5. **Complete Official Documentation**:
   - `README.md`
   - `docs/THIRD_PARTY_LICENSES.md`
   - `docs/guides/SIDERA_BUILD_RUN_AND_PUBLISH_GUIDE.md`
   - `docs/releases/SIDERA_ENGINE_1_0_RC1.md`
   - `docs/quality/SIDERA_M01_M42_COMPLETE_MILESTONE_AUDIT.md` (42-milestone evidence-based audit: 40 PASS, 3 PARTIAL, 0 FAIL)

## 3. Verification Results
- **Release & Debug Builds**: `sidera`, `sidera_editor`, `test_engine_1_0_release`, `test_rpg_framework` compiled cleanly in both Release and Debug.
- **M42 Release Tests (`test_engine_1_0_release.exe`)**: Passed 100% in both Release and Debug (version identity, schema compatibility, v1→v3 non-destructive migration with unknown-field preservation, future-version rejection, opening & closing `examples/rpg_reference_project`).
- **Historical M41 Evidence Reused**: 13 M29–M40 regression test suites + `test_rpg_framework` (Release & Debug) + 6-combination GPU smoke matrix (Forward / Deferred / Forward+ × Release / Debug, zero missing shader warnings when run from binary directory).
- **Distribution Package & Clean Install Test**:
  - Generated `build/packages/SIDERA-1.0.0-rc1-win64.zip` (`0.99 MB`).
  - Installed cleanly via `scripts/Install-SIDERA.ps1` (verified all 35 `.spv` shaders, `sidera.exe`, `sidera_editor.exe`, `vulkan-1.dll`, `examples/rpg_reference_project`, and `docs/THIRD_PARTY_LICENSES.md`).
  - Launched installed `sidera.exe --version` and `sidera.exe --smoke-test --render-path forward_plus` → `SIDERA Engine terminated with EXIT_SUCCESS.` (zero missing shader warnings).
  - Launched installed `sidera_editor.exe --version` and `sidera_editor.exe --open-project ..\examples\rpg_reference_project` → opened `RPG_Reference_Project` and shut down cleanly.
  - Uninstalled via `scripts/Uninstall-SIDERA.ps1` → target directory cleanly removed.

## 4. Known Limitations (`1.0.0-rc1`)
- Interactive Dear ImGui Vulkan Editor viewport/docking GUI rendering remains pending (`sidera_editor.exe` runs Editor Core & Content Editor APIs in CLI/headless mode).
- Vulkan 2D `UISystem` draw-command rasterization in `Renderer` remains pending.
- Only Windows x64 (MSVC 2022 + NVIDIA RTX 4060 Laptop GPU) has been built and verified; Linux/macOS are not verified.
- `.github/workflows/ci.yml` has not yet been executed on GitHub Actions.
