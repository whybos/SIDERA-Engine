# SIDERA Engine 1.0 — Third-Party Dependency & License Inventory

All third-party dependencies used by SIDERA Engine 1.0 (`1.0.0-rc1`) are fetched and pinned via CMake `FetchContent` in [CMakeLists.txt](../../CMakeLists.txt).

| Dependency | Version / Tag | Purpose | License | Distribution Requirements | Optional? |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Khronos Vulkan-Headers** | `v1.3.296` | Vulkan 1.3 C/C++ API headers | Apache-2.0 / MIT | Retain copyright & license notice (compile-time only) | No (unless system Vulkan SDK installed) |
| **Khronos Vulkan-Loader** | `v1.3.296` | Vulkan ICD loader (`vulkan-1.dll`) | Apache-2.0 | Include Apache-2.0 attribution with distributed `vulkan-1.dll` | No (unless system Vulkan SDK installed) |
| **GLFW** | `3.4` | Cross-platform windowing, input, Vulkan surface creation | zlib/libpng | Retain copyright notice in source/binary distributions | No (Runtime) |
| **GLM** | `1.0.1` | Header-only C++ math library (vectors, matrices, quaternions) | MIT (Happy Bunny) | Retain MIT copyright notice | No |
| **stb (`stb_image`)** | `master` (v2.30) | HDR/LDR image decoding for IBL sky & texture loading | Public Domain / MIT | No attribution required for public domain; MIT notice retained | No (Runtime) |
| **Khronos glslang** | `16.6.0` (Windows x64) | Build-time GLSL → SPIR-V shader compiler | BSD-3-Clause / Apache-2.0 | Build-time tool only; **not** redistributed in end-user packages | Build-time only (unless system `glslc` found) |
| **Microsoft Visual C++ Runtime** | VS 2022 (MSVC 19.x) | C++20 standard library runtime on Windows | Microsoft Visual Studio Redistributable | Standard Windows MSVC runtime | Platform runtime |

## Distribution Compliance Notes
- End-user packages (`SIDERA-1.0.0-rc1-win64.zip`) include this file (`docs/THIRD_PARTY_LICENSES.md`) and do **not** bundle build-only tools (`glslang.exe`, CMake caches, `.obj`/`.pdb` intermediates).
- `sidera_editor_core` and `sidera_framework` depend only on C++20 STL + `glm::glm`, keeping Editor and Framework builds decoupled from Vulkan/GLFW runtime binaries where not needed.
