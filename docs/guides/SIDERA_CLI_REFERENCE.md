# SIDERA Engine 1.0.0 — CLI Command Reference

All commands below have been tested on Windows x64 (`Release` and `Debug` builds, and installed packages). Invalid arguments return exit code `2` with a clear error message.

## 1. SIDERA Runtime (`sidera.exe`)
Run with the binary directory (`build\Release` or `<InstallDir>\bin`) as the working directory so `shaders\*.spv` resolves cleanly.

| Command | Behavior | Exit Code |
| :--- | :--- | :--- |
| `.\sidera.exe --help` | Print Runtime CLI usage | `0` |
| `.\sidera.exe --version` | Print `SIDERA Engine 1.0.0` | `0` |
| `.\sidera.exe --smoke-test --render-path forward` | Run Forward GPU smoke test + 2D Runtime HUD | `0` |
| `.\sidera.exe --smoke-test --render-path deferred` | Run Deferred GPU smoke test + 2D Runtime HUD | `0` |
| `.\sidera.exe --smoke-test --render-path forward_plus` | Run Forward+ GPU smoke test + 2D Runtime HUD | `0` |
| `.\sidera.exe --render-path invalid` | Print invalid render-path error | `2` |
| `.\sidera.exe --unknown-flag` | Print unknown argument error | `2` |

## 2. SIDERA Editor (`sidera_editor.exe`)

| Command | Behavior | Exit Code |
| :--- | :--- | :--- |
| `.\sidera_editor.exe` | Launch interactive GLFW + Vulkan + Dear ImGui Docking Editor | `0` |
| `.\sidera_editor.exe --help` | Print Editor CLI usage | `0` |
| `.\sidera_editor.exe --version` | Print `SIDERA Engine 1.0.0 (Editor)` | `0` |
| `.\sidera_editor.exe --open-project <dir>` | Launch graphical Editor and open `<dir>` | `0` |
| `.\sidera_editor.exe --gui-smoke --open-project <dir> --screenshot <path.bmp>` | Render live Vulkan + Dear ImGui docking frames, capture BMP screenshot, and exit | `0` |
| `.\sidera_editor.exe --headless-smoke --open-project <dir>` | Validate Editor Core services & project loading in headless mode | `0` |
| `.\sidera_editor.exe --unknown-flag` | Print unknown argument error | `2` |

## 3. Packaging, Installer & Standalone Game Build Scripts
- **Install Engine**: `.\scripts\Install-SIDERA.ps1 -SourceDir <unpackedZipDir> -InstallDir <targetDir> [-CreateStartMenuShortcut] [-CreateDesktopShortcut]`
- **Uninstall Engine**: `.\scripts\Uninstall-SIDERA.ps1 -InstallDir <targetDir>` (preserves user projects outside `<targetDir>`)
- **Build Standalone Game**: `.\scripts\Build-SideraGame.ps1 -ProjectDir <projectDir> -EngineBinDir <engineBinDir> -OutputDir <gameOutDir>`
