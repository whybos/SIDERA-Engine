# SIDERA Engine 1.0 — Build, Run, Package & Publish Guide (Windows PowerShell)

All commands below were verified on Windows 10/11 (x64) with Visual Studio 2022 (MSVC C++20) and CMake.

---

## 1. Prerequisites
- **OS**: Windows 10/11 x64
- **Compiler**: Visual Studio 2022 (Desktop development with C++ workload, MSVC C++20)
- **Build System**: CMake 3.24+ (bundled with Visual Studio 2022)
- **GPU**: Vulkan 1.3 capable GPU + up-to-date driver (verified on NVIDIA GeForce RTX 4060 Laptop GPU, Vulkan 1.4.341)
- **Git**: Git for Windows 2.40+

Optional: Vulkan SDK (if absent, CMake automatically fetches pinned Khronos `Vulkan-Headers v1.3.296`, `Vulkan-Loader v1.3.296`, `GLFW 3.4`, `GLM 1.0.1`, `stb`, and `glslang 16.6.0`).

---

## 2. Clone the Repository
```powershell
git clone https://github.com/whybos/SIDERA-Engine.git SIDERA
cd SIDERA
```

---

## 3. Configure CMake (Clean Configuration)
```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake -S . -B build -DSIDERA_BUILD_EDITOR=ON -DSIDERA_BUILD_FRAMEWORKS=ON -DSIDERA_BUILD_TESTS=ON -DSIDERA_BUILD_EXAMPLES=ON
```

---

## 4. Build Release & Debug

### Release Build
```powershell
& $cmake --build build --config Release --target sidera sidera_editor test_engine_1_0_release test_rpg_framework test_runtime_systems -j
```

### Debug Build
```powershell
& $cmake --build build --config Debug --target sidera sidera_editor test_engine_1_0_release test_rpg_framework -j
```

---

## 5. Run SIDERA Runtime (`sidera.exe`)
Always run `sidera.exe` with its binary folder as the working directory so relative `shaders/*.spv` paths resolve cleanly:

```powershell
# Interactive Forward+ (default)
Push-Location .\build\Release
.\sidera.exe --version
.\sidera.exe --render-path forward_plus

# Automated GPU smoke tests (Forward / Deferred / Forward+)
.\sidera.exe --smoke-test --render-path forward
.\sidera.exe --smoke-test --render-path deferred
.\sidera.exe --smoke-test --render-path forward_plus
Pop-Location
```

---

## 6. Launch SIDERA Editor & Open Reference Project
```powershell
.\build\Release\sidera_editor.exe --version
.\build\Release\sidera_editor.exe --headless-smoke
.\build\Release\sidera_editor.exe --open-project .\examples\rpg_reference_project
```

---

## 7. Run Automated Tests & M41 Reference Vertical Slice
```powershell
# Milestone 42 Release Validation (version, schema migration, reference project open)
.\build\Release\test_engine_1_0_release.exe

# Milestone 41 Reusable RPG/Adventure Framework + End-to-End Reference Vertical Slice (with Save/Reload)
.\build\Release\test_rpg_framework.exe

# Milestone 40 Runtime Systems
.\build\Release\test_runtime_systems.exe
```

---

## 8. Create Distribution Package (CPack ZIP)
```powershell
$cpack = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cpack.exe"
& $cpack --config .\build\CPackConfig.cmake -C Release -B .\build\packages
```
Output package: `build\packages\SIDERA-1.0.0-rc1-win64.zip`.

---

## 9. Install & Uninstall SIDERA Engine 1.0
```powershell
# Extract portable ZIP to a staging folder
Expand-Archive -Path .\build\packages\SIDERA-1.0.0-rc1-win64.zip -DestinationPath .\build\stage -Force

# Install to clean directory (and optionally create Start Menu shortcut)
.\scripts\Install-SIDERA.ps1 -SourceDir .\build\stage\SIDERA-1.0.0-rc1-win64 -InstallDir "$env:LOCALAPPDATA\SIDERA_Engine_1_0_rc1" -CreateStartMenuShortcut

# Launch installed Runtime & Editor
Push-Location "$env:LOCALAPPDATA\SIDERA_Engine_1_0_rc1\bin"
.\sidera.exe --smoke-test --render-path forward_plus
.\sidera_editor.exe --open-project ..\examples\rpg_reference_project
Pop-Location

# Uninstall cleanly
.\scripts\Uninstall-SIDERA.ps1 -InstallDir "$env:LOCALAPPDATA\SIDERA_Engine_1_0_rc1"
```

---

## 10. Git Status, Local Commit & Publishing to GitHub
Detected repository configuration:
- **Branch**: `master`
- **Remote**: `origin` → `https://github.com/whybos/SIDERA-Engine`

### Inspect Status & History
```powershell
git branch --show-current
git status --short --branch
git remote -v
git log -5 --oneline --decorate
```

### Push Local Commits to GitHub (Manual Step — Never Auto-Pushed)
```powershell
git push origin master
```
- **If upstream is not set**: `git push -u origin master`
- **If authentication fails**: Use Git Credential Manager (`git credential-manager`) or a GitHub Personal Access Token (PAT) with `repo` scope.
- **If remote is rejected (non-fast-forward)**: Run `git fetch origin` and `git rebase origin/master` (or inspect divergence with `git log --oneline --graph --all -10`) before pushing.
- **Verify on remote**:
```powershell
git ls-remote origin refs/heads/master
```

---

## 11. Troubleshooting
- **Missing shader warnings (`Shader not found: shaders/...`)**: Ensure the working directory is `build\Release` (or `<InstallDir>\bin`) where the `shaders\` folder containing all 35 `.spv` files is located.
- **`VK_LAYER_KHRONOS_validation` unavailable in Debug**: Expected when the Khronos Vulkan SDK is not installed system-wide; the engine falls back safely and continues rendering.
- **CMake cannot download FetchContent dependencies**: Ensure internet access or pre-install Vulkan SDK, GLFW 3.4, and GLM 1.0.1 locally.
