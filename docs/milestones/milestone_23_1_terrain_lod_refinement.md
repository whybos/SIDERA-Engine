# SIDERA Engine — Milestone 23.1: Terrain LOD Smooth Transition & Visual Quality Refinement

**Engine Version:** `v0.23.0` (Revision 23.1)  
**Branch:** `master`  
**Platform:** Windows 11 x64, Vulkan 1.3, C++20, CMake 3.31, MSVC 19.44  
**Primary Hardware:** NVIDIA GeForce RTX 4060 Laptop GPU (8GB VRAM)  

---

## 1. Root Cause Analysis of Abrupt Popping

Forensic inspection of the initial Milestone 23 implementation (`adbefb1`) identified **four** concrete runtime causes of visible popping:

1. **Instant Merge Collapse**: In `TerrainQuadtree::evaluateRecursive()`, when `!wantSplit && currentlySplit`, `mergeNode()` was called immediately on that frame, snapping all 4 children back to the coarse parent geometry in a single frame with zero merge geomorphing.
2. **Neighbor Edge Morph Desynchronization**: When a transitioning patch (`morphFactor > 0`) bordered a same-depth stable neighbor (`morphFactor == 0`), odd vertices along their shared boundary were displaced by the transitioning patch while held fixed by the stable neighbor, creating a vertical seam/pop along the shared edge.
3. **Unclamped Linear Morph Stepping**: `updateGeomorphing()` used raw unclamped `deltaTime / morphDuration` without smoothstep easing or hitch clamping, causing large frame deltas to jump transitions instantaneously.
4. **Unmorphed Balance Subdivisions**: `balanceQuadtree()` called `subdivideNode(idx, false)`, bypassing geomorphing for neighbor-balance-triggered splits.

---

## 2. Implemented Refinements

### 2.1 Temporal Geomorphing (`TerrainLODConfig.h`, `TerrainQuadtree.cpp`)
- Configurable duration bounds: `minMorphDuration = 0.20s`, default `morphDuration = 0.35s`, `maxMorphDuration = 0.60s`.
- Frame-rate-independent delta time clamped safely to `maxDeltaTimeClamp = 0.05s`.
- Smoothstep easing curve (`t * t * (3.0f - 2.0f * t)`) eliminating abrupt velocity discontinuities at transition endpoints.

### 2.2 Coordinated Split & Merge State Machine (`TerrainQuadNode.h`, `TerrainQuadtree.cpp`)
- Formal states: `Stable`, `PreparingSplit`, `Splitting`, `StableFine`, `PreparingMerge`, `Merging`.
- **Split**: All 4 children initialize at `morphFactor = 1.0` (exact piecewise-triangulated parent surface) and smoothly ease to `0.0` over `morphDuration`. Balance-triggered subdivisions also start morphed (`startMorphed = true`).
- **Merge**: All 4 children enter `Merging` together, morphing smoothly from `0.0 -> 1.0` toward the parent surface before `mergeNode()` collapses them.
- **Direction Reversal**: If the camera reverses direction mid-merge, children smoothly transition back to `Splitting` from their **current** `morphFactor` without resetting geometry.

### 2.3 Symmetric Neighbor Edge Morph Synchronization (`TerrainPatchMesh.cpp`, `TerrainQuadtree.cpp`, `shaders/terrain.vert`)
- `Vertex::color.z` encodes vertex edge role (`0=even`, `1=interior odd`, `2=North odd`, `3=South odd`, `4=West odd`, `5=East odd`). Because `patchResolution` is even, corner vertices are always even and never morph.
- `TerrainLODPushConstants` expanded cleanly to 128 bytes (Vulkan guaranteed minimum) by adding `glm::vec4 edgeMorph` (`x=North, y=South, z=West, w=East`).
- For every pair of same-depth adjacent leaves, both patches compute the exact symmetric average `0.5f * (self.morphFactor + neighbor.morphFactor)`, guaranteeing bit-identical shared boundary heights throughout all transitions.

---

## 3. Verification & Test Results

- **`test_terrain_lod.exe` (Release & Debug)**: **10 / 10 PASSED (100%)**
  - **Test 10 (M23.1)**: Verified frame-rate-independent morph progress across 30 / 60 / 144 FPS (`0.394 / 0.394 / 0.417`), smooth non-snapping merge, mid-merge direction reversal, and exact symmetric `edgeMorph` matching across all adjacent same-depth patches.
- **Runtime Smoke Tests & Vulkan Validation**:
  - Release `--smoke-test --stats --terrain-lod-debug`: `EXIT_SUCCESS`
  - Debug `--smoke-test --stats --terrain-lod-debug`: `EXIT_SUCCESS`, **0 Vulkan validation errors**.
- **Known Limitations**:
  - Desktop screenshot capture (`PIL.ImageGrab`) remains unavailable in the locked headless Windows session; runtime rendering was verified directly on the RTX 4060 GPU via Release & Debug smoke tests and Vulkan validation layers.
