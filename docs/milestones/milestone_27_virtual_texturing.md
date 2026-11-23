# SIDERA Engine — Milestone 27: Virtual Texturing System

**Engine Version:** `v0.27.0`  
**Milestone:** 27 — Virtual Texturing System  
**Branch:** `master`  
**Platform:** Windows 11 x64, Vulkan 1.3, C++20, CMake 3.31, MSVC 19.44  
**Primary Hardware:** NVIDIA GeForce RTX 4060 Laptop GPU (8GB VRAM)  

---

## 1. Architecture (`shaders/vt_feedback_resolve.comp`, `src/virtualtexturing/`)
- **Virtual Page Addressing (`VirtualTextureTypes.h`, `VirtualTexturePageTable.{h,cpp}`)**:
  - Deterministic 64-bit `VirtualTexturePageID` (`textureId | mipLevel | pageX | pageY`), supporting `128x128` and `256x256` tiles with configurable border gutters (`4px` default -> `136x136` physical tile) and gutter-corrected physical UV mapping for seam-free bilinear/trilinear/anisotropic filtering.
  - GPU-accessible std430 `PageTableEntry` (16 bytes, double-buffered across frames-in-flight) with bounded parent-mip fallback traversal (guaranteed termination).
- **Physical Page Cache (`VirtualTexturePhysicalCache.{h,cpp}`)**:
  - Software-managed physical page pool (no sparse residency required) with **LRU eviction**, pinned coarsest fallback mip protection, version-ID tracking, and 3-frame deferred reuse for frames-in-flight GPU safety.
- **GPU Feedback & Throttled Streaming (`shaders/vt_feedback_resolve.comp`, `VirtualTextureSystem.{h,cpp}`)**:
  - Compute shader evaluates virtual UV footprints, looks up resident pages, and atomically records missing-page requests with overflow protection.
  - Delayed frames-in-flight readback (zero synchronous GPU stall), CPU-side request deduplication, per-frame upload & byte budget throttling (`maxUploadsPerFrame`, `maxUploadBytesPerFrame`), and seamless gutter-padded procedural tile generation.

---

## 2. Verification & Test Results
- **`test_virtual_texturing.exe` (Release & Debug)**: **4 / 4 PASSED (100%)**
  1. `16384x16384` Virtual Texture creation, pinned fallback mips, parent-mip fallback resolution, and **1,363 MB** memory savings vs conventional mip chain: **PASSED**
  2. 50 duplicate requests deduplicated to 1 upload, direct cache hit after streaming: **PASSED**
  3. LRU page eviction (`7` evictions) while strictly protecting pinned coarsest fallback mips: **PASSED**
  4. Exact gutter texel continuity verified across adjacent `136x136` tiles: **PASSED**
- **Full Engine Regression Suite**: **82 / 82** unit tests passing across all 11 test executables.
- **Release & Debug Smoke Tests**: Live GPU compute dispatch of `vt_feedback_resolve.comp.spv` exited cleanly with `EXIT_SUCCESS` (`REL=0`, `DBG=0`).
- **Known Limitations**: Full asset-pipeline disk streaming is deferred to Milestone 35 per scope restrictions; `VK_LAYER_KHRONOS_validation` is not installed in the host system's Vulkan layer registry.
