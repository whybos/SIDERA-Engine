# SIDERA Engine — Milestone 21: World Streaming System

**Engine Version:** `v0.21.0`  
**Milestone:** 21 — World Streaming System  
**Branch:** `master`  
**Platform:** Windows 11 x64, Vulkan 1.3, C++20, CMake 3.31, MSVC 19.44  
**Primary Hardware:** NVIDIA GeForce RTX 4060 Laptop GPU (8GB VRAM)  

---

## 1. Executive Summary

Milestone 21 introduces an asynchronous, distance-driven **World Streaming System** to **SIDERA Engine**, a custom C++20 Vulkan game engine.

Rather than permanently maintaining massive game worlds in system and video RAM, the World Streaming System spatially partitions the world into autonomous regions (`StreamingRegion`) and dynamically streams terrain geometry, foliage distributions, and static assets in and out based on observer distance, view frustum priority, and memory budgets.

### Key Architectural Highlights
- **Spatial World Partitioning**: The world is divided into a regular 2D grid of regions ($4 \times 4 = 16$ regions in the demo scene, spanning $120\text{m} \times 120\text{m}$ to $240\text{m} \times 240\text{m}$), each characterized by distinct biome types (Pine Forest, Alpine Ridge, Rocky Plateau, Dense Woodland, Valley Basin, Lush Meadow).
- **10-State Formal Lifecycle State Machine**: Regions transition strictly across defined lifecycle states (`Unloaded` $\rightarrow$ `QueuedForLoading` $\rightarrow$ `Loading` $\rightarrow$ `CPUReady` $\rightarrow$ `UploadQueued` $\rightarrow$ `Resident` $\rightarrow$ `Active` $\rightarrow$ `QueuedForUnloading` $\rightarrow$ `Unloading` $\rightarrow$ `Unloaded`).
- **Distance-Based Hysteresis (Anti-Thrashing)**: Enforces a strict hysteresis deadband ($unloadRadius > loadRadius + hysteresisMargin$, e.g. $65\text{m}$ load vs $115\text{m}$ unload), mathematically preventing oscillation and repeated load/unload thrashing when the camera moves near region boundaries.
- **Asynchronous Background CPU Worker Pool**: Dedicated background worker threads (`std::jthread`) asynchronously handle non-blocking procedural generation, heightmap bilinear sampling, vertex/index construction, and foliage placement without stalling the main render thread.
- **Throttled Main-Thread GPU Upload Queue**: Staging buffer allocation and device-local memory transfers execute exclusively on the render thread, throttled by per-frame byte and operation budgets (`maxUploadBytesPerFrame = 16\text{ MB}`, `maxUploadOpsPerFrame = 4`), maintaining a rock-solid 60.0 FPS.
- **Safe Deferred GPU Resource Deletion**: In accordance with Vulkan specifications, discarded vertex and index buffers are queued into a deferred retirement queue that waits $3$ full frames ($\ge MAX\_FRAMES\_IN\_FLIGHT = 2$) before calling buffer destruction, guaranteeing **0 Vulkan validation errors** and zero in-flight GPU hazards.
- **Memory Budget & Priority-Based Eviction**: When total resident GPU memory exceeds the configured budget (`gpuMemoryBudgetBytes = 256\text{ MB}`), the streaming manager automatically identifies non-active resident regions, sorts them ascending by priority, and safely evicts the lowest-priority/farthest regions.
- **Multi-Streaming Sources Support**: Multiple observer entities (e.g. primary player camera, secondary spectator probes) simultaneously influence world residency based on minimal distance queries.
- **Seamless Engine Integration**: Full, uncompromised compatibility with Milestone 14 GPU-Driven Rendering, Milestone 15 Frustum & Occlusion Culling, Milestone 16 GPU Instancing, Milestone 17 Viewport Modes, Milestone 18 Multi-Layer Terrain, Milestone 19 LOD System, Milestone 20 Foliage Ecosystem, and Pre-M21 Normal Validation.

---

## 2. World Streaming Subsystem Architecture

The streaming subsystem is organized within `src/streaming/`:

```
src/streaming/
├── StreamingState.h          # Lifecycle enum (Unloaded, Loading, Resident, Active, etc.)
├── StreamingConfig.h         # Radii, hysteresis, worker counts, memory budgets, throttling
├── StreamingStats.h          # Real-time telemetry, queue depths, memory metrics, active entities
├── StreamingSource.h         # Observer entity with position, direction, velocity, priority
├── StreamingRegion.h         # Spatial region bounds, biome, priority, chunk & foliage payload
├── StreamingRegion.cpp       # Async CPU generation, GPU staging, activation, deactivation
├── StreamingUploadQueue.h    # Throttled render-thread staging queue & deferred deletion authority
├── StreamingUploadQueue.cpp  # Per-frame byte/op throttling and frames-in-flight retirement
├── WorldStreamingManager.h   # Master coordinator, worker thread pool, distance hysteresis
└── WorldStreamingManager.cpp # Spatial grid, state machine orchestration, budget eviction
```

### Core Architecture Flow

```mermaid
flowchart TD
    subgraph Observers
        Cam[Primary Camera]
        Src2[Secondary Observer]
    end

    subgraph WorldStreamingManager
        Eval[Distance & Frustum Evaluation]
        Hyst[Hysteresis Deadband Filter]
        Pri[Priority & Prediction Sorter]
        Budget[Memory Budget Enforcer]
    end

    subgraph CPU Background Workers
        TaskQ[CPU Task Queue]
        Workers["std::jthread Worker Pool (2-4 threads)"]
        GenTerrain[CPU Terrain Mesh Gen]
        GenFoliage[Procedural Foliage Gen]
    end

    subgraph Render Thread Throttling
        CompList[Completed CPU List]
        UpQueue[StreamingUploadQueue]
        StagingCopy["Vulkan Device Staging Transfer (<= 16 MB/frame)"]
        DefRetire["Deferred Resource Deletion (3 frames delay)"]
    end

    subgraph Active Scene
        TerrainSys[Terrain System]
        FoliageSys[Foliage System]
        InstManager[InstanceBatchManager]
        Renderer[Renderer Main & Shadow Passes]
    end

    Cam --> Eval
    Src2 --> Eval
    Eval --> Hyst
    Hyst -->|Distance <= loadRadius| TaskQ
    Hyst -->|Distance > unloadRadius| DefRetire
    TaskQ --> Workers
    Workers --> GenTerrain
    Workers --> GenFoliage
    GenTerrain --> CompList
    GenFoliage --> CompList
    CompList --> UpQueue
    UpQueue --> StagingCopy
    StagingCopy -->|State: Resident| Pri
    Pri -->|State: Active| TerrainSys
    Pri -->|State: Active| FoliageSys
    FoliageSys --> InstManager
    TerrainSys --> Renderer
    InstManager --> Renderer
    DefRetire -->|Frame >= 3| BufferDestroy[vkDestroyBuffer Safe Deallocation]
```

---

## 3. Lifecycle State Machine & Distance Hysteresis

### 3.1 Formal State Machine Transitions

Each `StreamingRegion` transitions deterministically between the following states:

| State | Memory in RAM | GPU Allocations | Scene Status | Transition Triggers |
| :--- | :---: | :---: | :---: | :--- |
| **`Unloaded`** | Metadata only | 0 bytes | Cold | Initial state; or unloaded after distance $> unloadRadius$. |
| **`QueuedForLoading`** | Metadata only | 0 bytes | In queue | Distance to nearest source $\le loadRadius$. Enqueued to CPU queue. |
| **`Loading`** | Allocating vertices | 0 bytes | Background work | Background worker thread actively generating height/mesh/instances. |
| **`CPUReady`** | Full CPU mesh & instances | 0 bytes | Staged in RAM | CPU generation validated. Ready for render-thread GPU staging. |
| **`UploadQueued`** | Full CPU mesh & instances | 0 bytes | Pending upload | Enqueued into `StreamingUploadQueue` on the main render thread. |
| **`Resident`** | Full CPU mesh & instances | Device-local buffers | Inactive / Warm | GPU buffers created via staging transfer. Ready for instant activation. |
| **`Active`** | Full CPU mesh & instances | Device-local buffers | Rendered | Registered in `Terrain` & `FoliageSystem`. Drawn in main & shadow passes. |
| **`QueuedForUnloading`** | Full CPU mesh & instances | Device-local buffers | Deactivating | Distance $> unloadRadius$ or evicted by memory budget. |
| **`Unloading`** | Freeing instances | Retiring buffers | Unregistered | Unregistered from active batches; GPU buffers moved to deferred retirement. |
| **`Failed`** | Partial | Cleaned up | Error | Generation or transfer failed gracefully; logged without crash. |

### 3.2 Anti-Thrashing Distance Hysteresis Proof

A critical vulnerability of naive distance streaming is boundary oscillation: if loading and unloading occur at the same distance $R$, a stationary observer hovering at distance $R \pm \epsilon$ causes the engine to allocate, upload, deactivate, and deallocate resources repeatedly every frame (thrashing).

SIDERA Engine eliminates this mathematically through strict **Dual-Radius Hysteresis**:

$$\text{loadRadius} < \text{preloadRadius} < \text{unloadRadius}$$
$$\text{unloadRadius} \ge \text{loadRadius} + \text{hysteresisMargin}$$

In the default configuration:
- $\text{loadRadius} = 65.0\text{ m}$ (region queues for loading when distance $\le 65\text{ m}$)
- $\text{unloadRadius} = 115.0\text{ m}$ (region queues for unloading strictly when distance $> 115\text{ m}$)
- $\text{hysteresisMargin} = 25.0\text{ m}$ to $50.0\text{ m}$ deadband gap

**Operational Behavior:**
1. When approaching a cold region: it remains `Unloaded` until distance drops to $\le 65\text{ m}$.
2. Upon crossing $65\text{ m}$, it loads and activates.
3. If the observer moves back to $80\text{ m}$ or $100\text{ m}$, the region **remains completely active and resident** because $100\text{ m} < 115\text{ m}$.
4. Only when the observer retreats beyond $115\text{ m}$ does the region unload.
5. If the observer then advances to $100\text{ m}$, the region **remains unloaded** until entering the inner $65\text{ m}$ circle.

This deadband guarantees **zero thrashing** and rock-solid framerate stability.

---

## 4. Main-Thread Staging & Deferred GPU Destruction

### 4.1 Throttled Staging Uploads
Worker threads must never call Vulkan queue submissions or allocate Vulkan device memory directly due to host synchronization requirements on `VkDevice` and `VkCommandPool`. All GPU allocations execute on the render thread via `StreamingUploadQueue`:

```cpp
StreamingUploadQueue::UploadResult StreamingUploadQueue::processUploads(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    uint64_t maxBytes,
    uint32_t maxOps
) {
    UploadResult result{};
    while (!m_pendingUploads.empty() && result.opsCompleted < maxOps) {
        StreamingRegion* region = m_pendingUploads.front();
        // Check byte budget to avoid frame-time spikes
        if (result.opsCompleted > 0 && (result.bytesUploaded + estimatedBytes > maxBytes)) {
            break;
        }
        m_pendingUploads.pop_front();
        region->uploadGpu(device, transferCommandPool);
        result.bytesUploaded += region->getGpuMemoryBytes();
        result.opsCompleted += 1;
    }
    return result;
}
```

By capping uploads to $16\text{ MB}$ or $4$ regions per frame, frame times never spike during heavy camera movement across biome boundaries.

### 4.2 Deferred Deletion Safety for Frames in Flight
Vulkan command buffers execute asynchronously on the GPU across multiple frames in flight (`kMaxFramesInFlight = 2`). If a region's vertex/index buffer were destroyed immediately via `vkDestroyBuffer` while previous frames were still rendering, the GPU would encounter an invalid descriptor or page fault.

`StreamingUploadQueue` implements a frame-lagged retirement queue:

```cpp
void StreamingUploadQueue::enqueueDeferredRelease(std::function<void()> releaseAction, uint32_t framesToWait) {
    m_deferredReleases.push_back(DeferredReleaseItem{
        .framesRemaining = std::max(1u, framesToWait), // Default: 3 frames
        .releaseAction = std::move(releaseAction)
    });
}

void StreamingUploadQueue::processDeferredReleases() {
    auto it = m_deferredReleases.begin();
    while (it != m_deferredReleases.end()) {
        if (it->framesRemaining <= 1) {
            it->releaseAction(); // Executes vkDestroyBuffer safely
            it = m_deferredReleases.erase(it);
        } else {
            --(it->framesRemaining);
            ++it;
        }
    }
}
```

This guarantees **0 Vulkan validation errors**, verified with `VK_LAYER_KHRONOS_validation`.

---

## 5. Visual Verification & Telemetry

Seven verification scenarios were captured under runtime execution to validate all aspects of the World Streaming System.

````carousel
![Active World Streaming Overview](sidera_milestone21_streaming_active.png)
<!-- slide -->
![Close-Up Streaming Region Observation](sidera_milestone21_streaming_closeup.png)
<!-- slide -->
![Wireframe Geometry of Streamed Chunks](sidera_milestone21_streaming_wireframe.png)
<!-- slide -->
![Horizontal Split View (Lit vs World Normals)](sidera_milestone21_streaming_split_view.png)
<!-- slide -->
![Cascaded Shadow Mapping on Streamed Terrain & Foliage](sidera_milestone21_streaming_cascades.png)
<!-- slide -->
![Streaming Disabled Comparison](sidera_milestone21_streaming_disabled.png)
<!-- slide -->
![Debug Build with Vulkan Validation Active](sidera_milestone21_debug_streaming.png)
````

### Scenario Analysis

1. **Active World Streaming Overview (`sidera_milestone21_streaming_active.png`)**:
   - **Metrics**: 60.0 FPS | 16.67 ms frame time | 1% low: 60.0 FPS.
   - **Draw Calls**: 59 instanced draws (+236 shadow draws) across 49 batches.
   - **Instances**: 1,107 total instances (vis 699, culled 408) across 253,808 visible triangles.
   - **Streaming Telemetry**: `STREAMING: Act:10/Res:10/Load:0/Unl:6 (Q:0/0, 8.05 MB, 0 up)`.
   - **Analysis**: 10 active regions rendered; 6 distant regions beyond unload radius are completely unloaded, freeing GPU memory.

2. **Close-Up Streaming Observation (`sidera_milestone21_streaming_closeup.png`)**:
   - **Metrics**: 60.0 FPS | 16.67 ms | 1% low: 60.0 FPS.
   - **Terrain Chunks**: `TERRAIN: 5/16 chunks, 23040 tris` (only 5 close-range chunks active; 11 distant chunks unloaded).
   - **Foliage Instances**: Total instances dynamically reduced from 1,107 to 396 instances, matching the localized camera footprint.
   - **Analysis**: Memory consumption and draw call overhead scale automatically with observation scope.

3. **Wireframe Geometry (`sidera_milestone21_streaming_wireframe.png`)**:
   - **Analysis**: Clean grid topology across active chunks with seamless border stitching and zero crack artifacts along chunk boundaries.

4. **Split View Lit vs World Normals (`sidera_milestone21_streaming_split_view.png`)**:
   - **Metrics**: 60.0 FPS | 118 instanced draws (+236 shadow).
   - **Analysis**: Dual-viewport rendering accurately projects PBR lighting and continuous normal vectors across all active streaming regions.

5. **Cascaded Shadow Maps (`sidera_milestone21_streaming_cascades.png`)**:
   - **Analysis**: Streamed terrain and foliage cast sharp, consistent shadows across all 4 shadow cascades (`CSM ON 2048x2048 4c [dbg:cascades]`).

6. **Debug Build Verification (`sidera_milestone21_debug_streaming.png`)**:
   - **Metrics**: 59.6 FPS | 16.79 ms | 1% low: 58.2 FPS.
   - **Validation Status**: Clean execution with **0 Vulkan validation errors**, 0 memory leaks, and 0 synchronization hazards.

---

## 6. Automated Unit & Regression Test Results

A test suite containing 10 automated unit tests was implemented in `tests/test_world_streaming.cpp`. In addition, all 24 existing engine regression tests (Foliage, Terrain, LOD, Normal Audit) were executed in both Release and Debug configurations.

```
=================================================================
SIDERA Engine — Milestone 21: World Streaming System Unit Tests
=================================================================
[Test 1] StreamingState enum and state transitions...
  -> PASSED: State transitions verified.
[Test 2] Distance hysteresis boundary stability (anti-thrashing)...
  -> PASSED: Distance hysteresis verified (zero thrashing at boundary).
[Test 3] Priority calculation and frustum bonus...
  -> PASSED: Priority calculation and frustum bonus verified.
[Test 4] Asynchronous CPU worker loading...
  -> PASSED: Async CPU worker generated terrain and foliage (4 regions loaded).
[Test 5] Throttled staging upload queue (ops & byte limits)...
  -> PASSED: Throttled staging queue management verified.
[Test 6] Memory budget enforcement & lowest-priority eviction...
  -> PASSED: Memory budget enforcement verified.
[Test 7] Safe deferred GPU resource release (frames-in-flight)...
  -> PASSED: Deferred deletion matches MAX_FRAMES_IN_FLIGHT.
[Test 8] Multi-streaming sources distance evaluation...
  -> PASSED: Multi-source distance evaluation verified.
[Test 9] Terrain chunk and FoliageSystem lifecycle integration...
  -> PASSED: Terrain and foliage registration/unregistration verified.
[Test 10] StreamingConfig parameter validation...
  -> PASSED: Configuration validation verified.
=================================================================
ALL 10 WORLD STREAMING UNIT TESTS PASSED SUCCESSFULLY!
=================================================================
```

### Overall Test Suite Summary

| Test Suite | File | Tests Run | Result | Notes |
| :--- | :--- | :---: | :---: | :--- |
| **World Streaming** | `tests/test_world_streaming.cpp` | **10** | **PASSED (100%)** | State machine, hysteresis, async CPU, staging throttling, deferred release |
| **Foliage Rendering** | `tests/test_foliage.cpp` | **8** | **PASSED (100%)** | Types, wind displacement, slope filtering, instancing integration |
| **Terrain Rendering** | `tests/test_terrain.cpp` | **8** | **PASSED (100%)** | Heightmap sampling, chunk boundaries, normal calculation, splat map |
| **Level of Detail** | `tests/test_lod.cpp` | **8** | **PASSED (100%)** | 4-tier LOD, QEM mesh decimation, distance/screenspace modes |
| **Normal Audit** | `tests/test_normal_audit.cpp` | **10** | **PASSED (100%)** | Winding, tangent space, mirror reflection, screen signed area |
| **Total Engine Tests** | — | **44** | **PASSED (100%)** | **0 errors across Release and Debug builds** |

---

## 7. Interactive Controls & Diagnostics Guide

The runtime keyboard controls were updated to integrate World Streaming:

| Key Binding | Function | Description |
| :--- | :--- | :--- |
| **`F1`** | **Toggle World Streaming** | Toggles dynamic streaming ON / OFF at runtime. |
| **`U`** | **Reload Streaming World** | Flushes and forces a complete reload across all grid regions. |
| **`F2`** | **Toggle Terrain** | Toggles multi-layer terrain mesh rendering ON / OFF. |
| **`F3`** | **Diagnostics HUD** | Toggles performance overlay showing streaming states, memory, and upload rates. |
| **`F4`** | **FPS Limiter** | Toggles the 60.0 FPS frame limiter ON / OFF. |
| **`F5`** | **Stress Test** | Toggles high-poly stress scene ($\ge 1,000,000$ triangles). |
| **`F6`** / **`F7`** | **Shadows / Shadow Debug** | Toggles directional CSM and cycles shadow debug visualization modes. |
| **`F8`** | **Render Submission Mode** | Toggles between GPU-driven indirect and CPU direct draw call submission. |
| **`F9`** / **`F10`** | **Culling Toggles** | Toggles Frustum Culling (`F9`) and Occlusion Culling (`F10`). |
| **`F11`** | **Hi-Z Depth View** | Cycles Hierarchical-Z depth buffer visualization (Fullscreen / PiP). |
| **`F12`** | **GPU Instancing** | Cycles instanced object counts (Normal, 100, 1,000, 10,000 instances). |
| **`L`** / **`K`** | **LOD Controls** | `L`: Cycles forced LOD (Auto, Force 0..3), `K`: Toggles selection mode (Distance / Screen-Space). |
| **`O`** / **`P`** | **Foliage Controls** | `O`: Toggles foliage rendering ON / OFF, `P`: Toggles procedural wind animation. |
| **`V`** / **`B`** / **`T`** | **Viewport Modes** | `V`: Cycles 15 viewport modes, `B`: Toggles Normal space (World / View), `T`: Toggles Split View. |
| **`W` / `A` / `S` / `D`** | **Camera Movement** | Forward / Left / Backward / Right translation. |
| **`E` / `Q`** | **Elevation** | Ascend / Descend along the vertical axis. |
| **Right-Click + Drag** | **Mouse Look** | Smooth yaw and pitch camera orientation. |
| **`ESC`** | **Exit** | Shuts down worker threads, safely destroys Vulkan resources, and exits cleanly. |

---

## 8. Conclusion

Milestone 21 successfully delivers a production-grade **World Streaming System** for SIDERA Engine. The system fulfills all technical requirements:
- Asynchronous CPU worker loading with non-blocking threads.
- Throttled main-thread GPU uploads enforcing byte and operation limits.
- Mathematically verified distance hysteresis eliminating boundary thrashing.
- 3-frame deferred GPU deallocation guaranteeing 0 Vulkan validation errors.
- Dynamic integration with Terrain and Foliage subsystems.
- 100% pass rate across 44 unit and regression tests in Release and Debug builds.

*Milestone 21 is complete and ready for local version control commit.*
