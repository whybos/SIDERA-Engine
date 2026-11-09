# SIDERA Engine v0.11.0 — Milestone 11: 1080p Rendering & High-Poly PBR Scene

**Status**: COMPLETE (Verified on NVIDIA GeForce RTX 4060 Laptop GPU, Vulkan 1.3)

---

## 1. Overview & Objectives

Milestone 11 scales the SIDERA rendering architecture to standard 1080p (1920x1080) Full HD presentation while dramatically escalating scene complexity to high-polygon 3D procedural models, verifying that the Cook-Torrance Physically Based Rendering (PBR) and Image-Based Lighting (IBL) systems maintain visual fidelity and 60 FPS performance under dense geometric workloads.

### Core Deliverables Achieved

1. **1920x1080 Default Rendering Pipeline**:
   - `WindowConfig` updated to default `1920x1080` in resizable windowed mode.
   - Primary monitor display resolution and usable desktop workarea queried (`glfwGetMonitorWorkarea`) to gracefully prevent OS taskbar clipping (`1920x1055` client framebuffer).
   - Dynamic swapchain, depth buffer, viewport, scissor, and camera aspect ratio tracking ensure proper presentation at any resolution.
   - Command-line overrides supported: `--width <w>`, `--height <h>`, `--unlimited`, `--stats`, `--stress-test`.

2. **Procedural High-Polygon 3D Mesh Engine (`ProceduralMeshes`)**:
   - **High-Resolution Icosphere** (`createIcosphere`): Geodesic icosahedron with recursive midpoint subdivision, normalized spherical projection, seamless texture coordinates, and analytical spherical normal generation. (Subdivisions 5: 20,480 triangles; Subdivisions 6: 81,920 triangles).
   - **Detailed Mechanical Gear** (`createGear`): Involute-like tooth profile with 8 perimeter steps per tooth, cylindrical central bore, hub wall, recessed web structure, and beveled face transitions. (30 teeth: 32,480 triangles; 40 teeth: 50,240 triangles).
   - **High-Polygon Torus** (`createTorus`): Smooth parametric torus with analytical normal vectors and seamless UV unwrapping. (144x64: 18,432 triangles; 320x128: 81,920 triangles).
   - **Procedural Torus Knot** (`createTorusKnot`): Trefoil $(p=2, q=3)$ 3D space curve generated using an orthonormal parallel-transport (Bishop-like) reference frame to eliminate torsional twisting. (360x64: 46,080 triangles; 720x128: 184,320 triangles).
   - **Detailed Multi-Part Turbine Rotor** (`createTurbine`): Aerodynamic assembly consisting of a parabolic nose cone, cylindrical hub body, 30–48 twisted airfoil blades (cambered NACA profile with progressive radial twist), and outer cylindrical retaining shroud ring. (30 blades: ~38,000 triangles; 48 blades: ~80,000 triangles).

3. **Geometry Density Targets Surpassed**:
   - **Normal High-Quality Scene**: **273,688 visible triangles** across 24 draw calls ($\ge 250,000$ target passed).
   - **Stress-Test Mode**: **1,114,904 visible triangles** across 26 draw calls ($\ge 1,000,000$ target passed), togglable at runtime via `F5` hotkey or `--stress-test` CLI argument.
   - Zero CPU bottlenecking: Efficient GPU-backed index and vertex buffer reuse maintains minimal draw calls (24–26 calls).

4. **Cook-Torrance PBR & IBL Verification**:
   - Preserved all Milestone 10 IBL precomputation passes (equirectangular to cubemap, diffuse irradiance convolution, specular prefiltered environment map with 5 mip levels, 512x512 BRDF integration LUT).
   - Material showcase:
     - *Mirror Chrome Torus Knot*: $metallic=1.0, roughness=0.04$
     - *Polished Brass / Gold Gear*: $metallic=1.0, roughness=0.12$
     - *Machined Titanium Turbine*: $metallic=1.0, roughness=0.20$
     - *Brushed Copper Torus Knot*: $metallic=1.0, roughness=0.28$
     - *Platinum Torus*: $metallic=1.0, roughness=0.08$
     - *Industrial Gunmetal Gear*: $metallic=1.0, roughness=0.40$
     - *Smooth Emerald Dielectric Icosphere*: $metallic=0.0, roughness=0.05$
     - *Frosted Ruby Dielectric Icosphere*: $metallic=0.0, roughness=0.40$
     - *Roughness Progression Spheres*: 10 foreground spheres ($roughness=0.08 \to 0.90$) for both dielectric and metallic response.

5. **Performance & Real-Time Monitoring**:
   - F3 performance statistics overlay updated with resolution extent, present mode, draw call count, total triangle count, and STRESS MODE indicator.
   - F4 frame limiter locks frametimes to 16.67 ms (60 FPS).
   - F5 hotkey allows instantaneous switching between normal (273K tris) and stress mode (1.11M tris).

---

## 2. Visual Results

### Default 1080p High-Poly Scene (273,688 Triangles)
![1080p Scene](sidera_milestone11_scene_1080p.png)

*Normal high-quality scene rendered at 1080p Full HD featuring the central machined titanium turbine, mirror chrome torus knot, gold mechanical gear, emerald and ruby icospheres, platinum torus, and foreground PBR spheres under procedural physical HDR IBL lighting.*

### Performance & F3 Statistics (Locked 60 FPS Target at 1080p)
![1080p Stats](sidera_milestone11_stats_1080p.png)

*Live F3 statistics in window title: **59.9 FPS** (16.69 ms frame time, min 16.67 ms, max 17.10 ms, 1% low 58.5 FPS) at 1920x1055 FIFO(vsync), 24 draw calls, 273,688 visible triangles.*

### High-Poly Stress-Test Mode (1,114,904 Triangles)
![Stress Test 1M Tris](sidera_milestone11_stress_1m_tris.png)

*High-poly stress-test mode rendering over **1.11 Million triangles** at locked **59.8 FPS** (16.73 ms frame time, 1% low 57.9 FPS) across 26 draw calls with the F5 STRESS MODE indicator enabled.*

### Procedural Turbine Rotor & Torus Knot Close-Up
![Turbine & Torus Knot Closeup](sidera_milestone11_closeup.png)

*Macro camera view highlighting individual twisted aerodynamic airfoil blades, parabolic nose cone reflection, outer shroud ring, and the smooth continuous curvature of the trefoil torus knot.*

### Mechanical Gear Geometry & Teeth Detail
![Mechanical Gear Closeup](sidera_milestone11_gear_closeup.png)

*Direct overhead view of the high-poly mechanical gear detailing the involute tooth profile (30 teeth with 8 radial steps each), central bore, cylindrical hub wall, and recessed web.*

### Multi-Material Metallic Reflection Showcase
![Metallic Reflections](sidera_milestone11_metallic_reflection.png)

*Angled reflection perspective demonstrating specular reflection variations across polished chrome, machined titanium, polished gold brass, and platinum.*

---

## 3. Performance Metrics Summary

| Scene Mode | Triangle Count | Draw Calls | Resolution | FPS (Limiter ON) | Avg Frame Time | 1% Low FPS | Limiter OFF Min Frame Time |
|---|---|---|---|---|---|---|---|
| **Normal High-Poly** | **273,688** | 24 | 1920x1055 (1080p) | **59.9 FPS** | 16.69 ms | 58.5 FPS | 14.14 ms (~70.7 FPS raw) |
| **Stress-Test Mode** | **1,114,904** | 26 | 1920x1055 (1080p) | **59.8 FPS** | 16.73 ms | 57.9 FPS | 14.80 ms (~67.5 FPS raw) |

---

## 4. Verification & Acceptance Criteria

| Criteria | Status | Details |
|---|---|---|
| 1080p Default Resolution | **PASS** | Window defaults to 1920x1080, queries monitor workarea, tracks resize dynamically |
| Normal Geometry Target ($\ge 250\text{k}$ tris) | **PASS** | 273,688 triangles rendered cleanly across 24 draw calls |
| Stress-Test Target ($\ge 1\text{M}$ tris) | **PASS** | 1,114,904 triangles rendered cleanly across 26 draw calls, toggled via F5 or `--stress-test` |
| Procedural Mesh Generator | **PASS** | Icosphere, Gear, Torus, Torus Knot, and Turbine implemented in `ProceduralMeshes` |
| PBR & IBL Preservation | **PASS** | Cook-Torrance BRDF and 4-pass IBL precomputation fully functional across all objects |
| Frame Limiter & F3 Statistics | **PASS** | Rock solid 60 FPS target maintained, live metrics in title bar |
| Automated Smoke Test | **PASS** | 1080p startup, input processing, 720p resize, minimize, restore, 1080p resize, clean exit |
| Visual Verification Screenshots | **PASS** | 6 screenshots captured and archived in `docs/milestones/` |

---

## 5. Git Commit
- `feat(renderer): add 1080p high-poly PBR scene`
