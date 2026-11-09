# SIDERA Engine v0.9.0 — Milestone 09: PBR Foundation + F3 Performance Stats

**Status**: COMPLETE (RTX 4060 Laptop GPU, Vulkan 1.3)

## PBR
- `Vertex` extended with `vec3 normal` (location 3, stride 44 B). Normals on cube, pyramid, ground; new `Mesh::createSphere` (UV sphere, analytical smooth normals).
- Scene UBO (`SceneUniformData`): view/proj/viewProj, camera position, directional sun, 2 point lights (inverse-square falloff).
- Push constants (96 B, vertex+fragment): model, albedo + metallic, roughness + AO + texture strength.
- `triangle.frag`: Cook-Torrance BRDF (GGX distribution, Smith-Schlick geometry, Fresnel-Schlick), energy-conserving diffuse/specular split, hemisphere ambient + rough specular ambient, Reinhard tone mapping (sRGB swapchain applies gamma).
- Showcase: 2 rows x 5 spheres (dielectric red / metallic gold) with roughness 0.1 -> 0.9, textured cube, polished metal pyramid, rough dielectric pyramid, textured ground.

## F3 stats / frame limiter
- `F3` toggles stats shown in the window title: FPS, target, avg/min/max frame time, 1% low, resolution + present mode, draw calls, triangles, limiter state. `--stats` starts with it enabled.
- `F4` toggles the 60 FPS frame limiter (`ApplicationConfig::targetFps = 60`). Limiter = sleep + short yield-spin.
- `FrameStats` (0.5 s window, no per-frame allocations).

## Verification
- Build OK; `--smoke-test --stats` passed: ~59.7-62.0 FPS with limiter ON on FIFO (vsync); dips only during the scripted resize/minimize steps.
- Screenshots: `sidera_milestone09_pbr_foundation.png`, `sidera_milestone09_stats_f3.png`.
- Not verified: the F3/F4 key press path itself (stats were exercised via `--stats`); validation layer is not installed on this host.
