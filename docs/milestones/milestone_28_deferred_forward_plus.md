# SIDERA Engine — Milestone 28: Deferred / Forward+ Rendering

**Engine Version:** `v0.28.0`  
**Milestone:** 28 — Deferred / Forward+ Rendering  
**Branch:** `master`  
**Platform:** Windows 11 x64, Vulkan 1.3, C++20, CMake 3.31, MSVC 19.44  
**Primary Hardware:** NVIDIA GeForce RTX 4060 Laptop GPU (8GB VRAM)  

---

## 1. Architecture (`src/renderer/lighting/`, `shaders/`)
- **Hybrid Rendering Path (`RenderPath.h`)**: Runtime-selectable `Forward`, `Deferred`, and `ForwardPlus` paths (`--render-path forward|deferred|forward-plus`), preserving existing Forward PBR + CSM + IBL + Terrain + Virtual Texturing + GPU Vegetation pipelines.
- **Light Management (`LightTypes.h`)**: std430-aligned `GPULight` (64 bytes, `static_assert` verified) supporting up to 512 Directional, Point, and Spot lights with enable/disable, removal, and dirty-tracked GPU uploads.
- **Vulkan 1.3 G-Buffer (`GBuffer.{h,cpp}`)**: 4 attachments (`Albedo` RGBA8, `Normal` HDR RGBA16F, `Material` Rough/Metal/AO RGBA8, `Emissive` HDR RGBA16F) with Vulkan 1.3 Synchronization2 layout transitions and automatic resize support.
- **GPU 16×16 Tiled Light Culling (`shaders/light_culling.comp`, `TiledLightCuller.{h,cpp}`)**:
  - `local_size_x = 16, local_size_y = 16` compute pass assigning Directional, Point, and Spot lights to screen-space tiles (`120×68` tiles at 1080p) using shared memory + atomic compaction with strict 64-light/tile overflow protection and `COMPUTE_SHADER -> FRAGMENT_SHADER` Synchronization2 barriers.
- **Deferred PBR Lighting (`shaders/deferred_lighting.vert`, `shaders/deferred_lighting.frag`)**:
  - Fullscreen triangle Cook-Torrance GGX PBR lighting pass reading G-Buffer attachments + tiled light lists, plus built-in debug visualization modes (Albedo, Normal, Roughness, Metallic, AO, Emissive, Tile Light Heatmap).

---

## 2. Verification & Test Results
- **`test_deferred_forward_plus.exe` (Release & Debug)**: **3 / 3 PASSED (100%)**
  1. Directional/Point/Spot CRUD & dirty tracking: **PASSED**
  2. 16×16 tiled light culling across **1, 17, 65, 129, and 257 lights** at 1080p (`120×68` tiles):
     - `1 light`: avg `1.0` / max `1`, `0.034 ms`
     - `17 lights`: avg `4.96` / max `7`, `0.533 ms`
     - `65 lights`: avg `17.90` / max `28`, `2.034 ms`
     - `129 lights`: avg `36.55` / max `55`, `4.306 ms`
     - `257 lights`: avg `60.78` / max `64` (strictly clamped, overflow tracked), `9.016 ms`
  3. Viewport resize (`720p -> 80×45`, `1440p -> 160×90`) & `RenderPath` switching: **PASSED**
- **Full Engine Regression Suite**: **85 / 85** unit tests passing across all 12 test executables.
- **Release & Debug Smoke Tests**: `--render-path forward-plus` (Release & Debug) and `--render-path deferred` (Release) exited cleanly with `EXIT_SUCCESS`.
- **Known Limitations**: Advanced Post Processing, Volumetric Rendering, GI, and Reflections are deferred to Milestones 29–32 per scope restrictions.
