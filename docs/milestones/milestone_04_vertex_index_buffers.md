# SIDERA Engine v0.4.0 — Milestone 04: Vertex & Index Buffer System

## 1. Architecture Changes

Milestone 04 replaces procedural shader-generated geometry (`gl_VertexIndex`) with hardware-backed GPU Vertex and Index Buffers (`VkBuffer` + `VkDeviceMemory`) uploaded to `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` memory via a transient `HOST_VISIBLE | HOST_COHERENT` staging buffer.

```text
SIDERA/
├── CMakeLists.txt
├── docs/
│   └── milestones/
│       ├── milestone_04_vertex_index_buffers.md
│       └── sidera_milestone04_vertex_index_buffers.png
├── shaders/
│   ├── triangle.vert
│   └── triangle.frag
└── src/
    ├── main.cpp
    ├── core/
    │   ├── Application.h / Application.cpp
    │   ├── Log.h / Log.cpp
    │   └── Window.h / Window.cpp
    └── renderer/
        ├── Vertex.h
        ├── Renderer.h / Renderer.cpp
        └── vulkan/
            ├── GraphicsPipeline.h / GraphicsPipeline.cpp
            ├── VulkanBuffer.h / VulkanBuffer.cpp
            ├── VulkanCheck.h
            ├── VulkanContext.h / VulkanContext.cpp
            ├── VulkanDevice.h / VulkanDevice.cpp
            ├── VulkanFrameSync.h / VulkanFrameSync.cpp
            ├── VulkanInstance.h / VulkanInstance.cpp
            ├── VulkanShader.h / VulkanShader.cpp
            └── VulkanSwapchain.h / VulkanSwapchain.cpp
```

### Resource Destruction Order
On shutdown, `vkDeviceWaitIdle` is called before destroying resources in strict reverse dependency order:
$$\text{Command Pools} \rightarrow \text{Index Buffer} \rightarrow \text{Vertex Buffer} \rightarrow \text{GraphicsPipeline} \rightarrow \text{PipelineLayout} \rightarrow \text{FrameSync} \rightarrow \text{Swapchain ImageViews} \rightarrow \text{Swapchain} \rightarrow \text{LogicalDevice} \rightarrow \text{Surface} \rightarrow \text{DebugMessenger} \rightarrow \text{Instance} \rightarrow \text{Window}$$

---

## 2. New Files

- `src/renderer/Vertex.h` — Defines `sidera::Vertex` (`position`, `color`) and static Vulkan vertex input binding and attribute descriptors.
- `src/renderer/vulkan/VulkanBuffer.h` — Declares the RAII `sidera::VulkanBuffer` abstraction for `VkBuffer` and `VkDeviceMemory`.
- `src/renderer/vulkan/VulkanBuffer.cpp` — Implements `VulkanBuffer` creation, memory type selection, host mapping with non-coherent atom flushing (`vkFlushMappedMemoryRanges`), staging-to-device-local transfers (`vkCmdCopyBuffer` synchronized via `VkFence`), and RAII teardown.
- `docs/milestones/milestone_04_vertex_index_buffers.md` — Milestone 04 technical documentation.
- `docs/milestones/sidera_milestone04_vertex_index_buffers.png` — Captured screenshot of the GPU-rendered indexed triangle.

---

## 3. Modified Files

- `shaders/triangle.vert` — Replaced `gl_VertexIndex` arrays with vertex attribute inputs `layout(location = 0) in vec3 inPosition` and `layout(location = 1) in vec3 inColor`.
- `src/renderer/vulkan/GraphicsPipeline.cpp` — Updated `VkPipelineVertexInputStateCreateInfo` to consume `Vertex::getBindingDescription()` and `Vertex::getAttributeDescriptions()`.
- `src/renderer/Renderer.h` & `src/renderer/Renderer.cpp` — Added `m_vertexBuffer`, `m_indexBuffer`, `createGeometryBuffers()`, `destroyGeometryBuffers()`, and updated `recordCommandBuffer()` to bind vertex/index buffers and call `vkCmdDrawIndexed(cmd, 3, 1, 0, 0, 0)`.
- `src/core/Application.h`, `src/main.cpp`, `src/renderer/vulkan/VulkanInstance.cpp`, & `CMakeLists.txt` — Updated build targets and engine version to `v0.4.0`.

---

## 4. Buffer Memory Strategy

`sidera::VulkanBuffer` (`src/renderer/vulkan/VulkanBuffer.cpp`) implements a two-stage upload pipeline for static/immutable GPU geometry (`VulkanBuffer::createDeviceLocalBuffer`):

1. **Memory Type Query (`VulkanBuffer::findMemoryType`)**:
   - Queries `vkGetPhysicalDeviceMemoryProperties` and inspects `memoryRequirements.memoryTypeBits`.
   - Performs a two-pass search: first matching `requiredProperties | preferredProperties`, and falling back to `requiredProperties` if the preferred flags are unavailable on the target GPU.
2. **Staging Buffer Allocation & Upload**:
   - Allocates a temporary `VkBuffer` with `VK_BUFFER_USAGE_TRANSFER_SRC_BIT` in `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` memory (preferring `VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`).
   - Maps the memory via `vkMapMemory`, copies the CPU payload, and if the memory type is not `HOST_COHERENT`, aligns the flush range to `VkPhysicalDeviceLimits::nonCoherentAtomSize` and calls `vkFlushMappedMemoryRanges` before unmapping.
3. **Device-Local Buffer Allocation & GPU Copy**:
   - Allocates the destination `VkBuffer` with `usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT` in `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` memory.
   - Records `vkCmdCopyBuffer` into a one-time-submit primary command buffer, submits it via Vulkan 1.3 `vkQueueSubmit2` signaling a dedicated transfer `VkFence`, and waits on that fence (`vkWaitForFences`).
   - Once the transfer fence signals, the temporary command buffer, fence, and staging buffer are immediately destroyed via RAII.

---

## 5. Vertex Structure

Defined in `src/renderer/Vertex.h`:

```cpp
struct Vertex {
    glm::vec3 position{0.0f, 0.0f, 0.0f}; // Offset: 0 bytes,  Format: VK_FORMAT_R32G32B32_SFLOAT
    glm::vec3 color{1.0f, 1.0f, 1.0f};    // Offset: 12 bytes, Format: VK_FORMAT_R32G32B32_SFLOAT
};
```

- **Binding**: `0`
- **Stride**: `sizeof(Vertex) == 24` bytes (`VK_VERTEX_INPUT_RATE_VERTEX`)
- **Attributes**:
  - `location = 0`: `position` (`VK_FORMAT_R32G32B32_SFLOAT`, `offset = 0`)
  - `location = 1`: `color` (`VK_FORMAT_R32G32B32_SFLOAT`, `offset = 12`)
- **Initial Triangle Vertices (72 bytes total)**:
  - Vertex 0: `position = (-0.5, -0.5, 0.0)`, `color = (1.0, 0.0, 0.0)` (Red)
  - Vertex 1: `position = ( 0.5, -0.5, 0.0)`, `color = (0.0, 1.0, 0.0)` (Green)
  - Vertex 2: `position = ( 0.0,  0.5, 0.0)`, `color = (0.0, 0.0, 1.0)` (Blue)

---

## 6. Index Structure

- **Element Type**: `uint32_t` (`VK_INDEX_TYPE_UINT32`)
- **Indices (12 bytes total)**: `{ 0, 1, 2 }`
- **Buffer Usage**: `VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT` in `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`.

---

## 7. Rendering Command Sequence

Per frame in `Renderer::drawFrame` and `Renderer::recordCommandBuffer`:

1. Wait on `inFlightFence[m_currentFrame]` and acquire the next swapchain image (`vkAcquireNextImageKHR`).
2. Reset `inFlightFence[m_currentFrame]` and reset `m_commandPools[m_currentFrame]`.
3. Begin recording primary command buffer (`VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT`).
4. Transition swapchain image from `VK_IMAGE_LAYOUT_UNDEFINED` to `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` (`vkCmdPipelineBarrier2`).
5. Begin Vulkan 1.3 Dynamic Rendering (`vkCmdBeginRendering`) with clear color `[0.04, 0.08, 0.16, 1.00]`.
6. Bind graphics pipeline (`vkCmdBindPipeline`).
7. Set dynamic viewport (`vkCmdSetViewport`) and dynamic scissor (`vkCmdSetScissor`) to the swapchain extent.
8. Bind vertex buffer (`vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets)`).
9. Bind index buffer (`vkCmdBindIndexBuffer(cmd, m_indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT32)`).
10. Issue indexed draw call (`vkCmdDrawIndexed(cmd, 3, 1, 0, 0, 0)`).
11. End dynamic rendering (`vkCmdEndRendering`).
12. Transition swapchain image from `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` (`vkCmdPipelineBarrier2`).
13. End command buffer, submit via `vkQueueSubmit2`, and present via `vkQueuePresentKHR`.

---

## 8. Build Results

- **Compiler**: MSVC `19.44.35213.0` (`Visual Studio 17 2022`, `x64`, `/std:c++20 /W4 /permissive-`)
- **SPIR-V Shaders**: `triangle.vert.spv` (`980 bytes`), `triangle.frag.spv` (`500 bytes`)
- **Executable**: `build/Debug/sidera.exe`
- **Errors / Warnings**: **0 errors, 0 warnings**

---

## 9. Runtime Test Results

```text
[18:03:50.573] [INFO ] [Core] Starting SIDERA Engine v0.4.0...
[18:03:52.408] [INFO ] [Window] Created window 'SIDERA Engine v0.4.0 - Vertex & Index Buffers' (1280x720 px)
[18:03:52.427] [WARN ] [Vulkan] Validation layer status: UNAVAILABLE ('VK_LAYER_KHRONOS_validation' requested in development build, but not found in system Vulkan layers).
[18:03:52.439] [ERROR] [VulkanValidation] Failed to open dynamic library "C:\ProgramData\ReShade\.\ReShade64.dll" with error 1114
[18:03:52.588] [INFO ] [Vulkan] Vulkan instance created (API target: 1.3, active extensions: 3, active layers: 0).
[18:03:52.588] [INFO ] [Vulkan] Debug utils messenger attached (VK_EXT_debug_utils).
[18:03:52.588] [INFO ] [Vulkan] Window surface created.
[18:03:52.769] [INFO ] [Vulkan] Discovered 5 Vulkan physical device(s):
[18:03:52.770] [INFO ] [Vulkan]   [GPU 0] Intel(R) UHD Graphics 770 (Integrated GPU, Vulkan 1.4.323) -> Score: 6916
[18:03:52.770] [INFO ] [Vulkan]   [GPU 1] Microsoft Direct3D12 (Intel(R) UHD Graphics) (Integrated GPU, Vulkan 1.2.359) -> Score: 0
[18:03:52.770] [INFO ] [Vulkan]   [GPU 2] NVIDIA GeForce RTX 4060 Laptop GPU (Discrete GPU, Vulkan 1.4.341) -> Score: 15932
[18:03:52.770] [INFO ] [Vulkan]   [GPU 3] Microsoft Direct3D12 (NVIDIA GeForce RTX 4060 Laptop GPU) (Discrete GPU, Vulkan 1.2.359) -> Score: 0
[18:03:52.770] [INFO ] [Vulkan]   [GPU 4] Microsoft Direct3D12 (Microsoft Basic Render Driver) (CPU / Software, Vulkan 1.2.359) -> Score: 0
[18:03:52.770] [INFO ] [Vulkan] Selected Physical GPU: 'NVIDIA GeForce RTX 4060 Laptop GPU' (Discrete GPU, API 1.4.341, Graphics Queue Family: 0, Present Queue Family: 0)
[18:03:52.802] [INFO ] [Vulkan] Logical device created (Vulkan 1.3 Dynamic Rendering: ENABLED, Synchronization2: ENABLED).
[18:03:52.802] [INFO ] [Vulkan] VulkanContext initialized successfully.
[18:03:52.807] [INFO ] [Swapchain] Surface capabilities: minImageCount=2, maxImageCount=8, currentExtent=1280x720, supportedFormats=5, supportedPresentModes=5
[18:03:52.895] [INFO ] [Swapchain] Created swapchain: 1280x720 px | Images: 3 | Format: VK_FORMAT_B8G8R8A8_SRGB | ColorSpace: VK_COLOR_SPACE_SRGB_NONLINEAR_KHR | PresentMode: VK_PRESENT_MODE_FIFO_KHR
[18:03:52.895] [INFO ] [FrameSync] Created synchronization primitives (2 frames in flight, 3 per-image present semaphores).
[18:03:52.895] [INFO ] [Shader] Created shader module from 'triangle.vert.spv' (980 bytes).
[18:03:52.895] [INFO ] [Shader] Created shader module from 'triangle.frag.spv' (500 bytes).
[18:03:52.908] [INFO ] [Pipeline] Graphics pipeline created successfully (Vulkan 1.3 Dynamic Rendering).
[18:03:52.909] [INFO ] [Shader] Destroying Vulkan shader module...
[18:03:52.909] [INFO ] [Shader] Destroying Vulkan shader module...
[18:03:52.909] [INFO ] [Renderer] Created 2 command pools and primary command buffers.
[18:03:52.909] [INFO ] [Buffer] Created DEVICE_LOCAL Vertex Buffer (3 vertices, 72 bytes, stride=24 bytes).
[18:03:52.910] [INFO ] [Buffer] Created DEVICE_LOCAL Index Buffer (3 indices, 12 bytes, type=VK_INDEX_TYPE_UINT32).
[18:03:52.910] [INFO ] [Renderer] Renderer initialized (ClearColor: [0.04, 0.08, 0.16, 1.00], FramesInFlight: 2).
[18:03:52.910] [INFO ] [Core] SIDERA Engine initialized.
[18:03:52.910] [INFO ] [Core] Entering frame loop (press ESC or close window to exit).
[18:03:53.419] [INFO ] [Core] [SmokeTest] Resizing window to 1024x576...
[18:03:53.476] [INFO ] [Swapchain] Swapchain recreated (1024x576 px, 3 images).
[18:03:53.813] [INFO ] [Core] [SmokeTest] Minimizing window...
[18:03:54.240] [INFO ] [Core] [SmokeTest] Restoring window...
[18:03:54.246] [INFO ] [Swapchain] Swapchain recreated (1024x576 px, 3 images).
[18:03:54.619] [INFO ] [Core] [SmokeTest] Resizing window back to 1280x720...
[18:03:54.675] [INFO ] [Swapchain] Swapchain recreated (1280x720 px, 3 images).
[18:03:55.119] [INFO ] [Core] [SmokeTest] Frame loop verification complete; requesting close.
[18:03:55.153] [INFO ] [Core] Frame loop exited cleanly.
[18:03:55.153] [INFO ] [Core] SIDERA Engine shutting down...
[18:03:55.153] [INFO ] [Renderer] Shutting down Renderer (total rendered frames: 120)...
[18:03:55.153] [INFO ] [Renderer] Destroying command pools...
[18:03:55.153] [INFO ] [Buffer] Destroying Vulkan Index Buffer...
[18:03:55.153] [INFO ] [Buffer] Destroying Vulkan Vertex Buffer...
[18:03:55.154] [INFO ] [Pipeline] Destroying Vulkan graphics pipeline...
[18:03:55.154] [INFO ] [Pipeline] Destroying Vulkan pipeline layout...
[18:03:55.154] [INFO ] [FrameSync] Destroying frame synchronization primitives...
[18:03:55.154] [INFO ] [Swapchain] Destroying swapchain image views (3)...
[18:03:55.154] [INFO ] [Swapchain] Destroying Vulkan swapchain...
[18:03:55.183] [INFO ] [Vulkan] Shutting down VulkanContext...
[18:03:55.184] [INFO ] [Vulkan] Destroying Vulkan logical device...
[18:03:55.202] [INFO ] [Vulkan] Destroying Vulkan window surface...
[18:03:55.202] [INFO ] [Vulkan] Destroying Vulkan debug messenger...
[18:03:55.202] [INFO ] [Vulkan] Destroying Vulkan instance...
[18:03:55.223] [INFO ] [Window] Destroying GLFW window...
[18:03:55.237] [INFO ] [Window] GLFW terminated cleanly.
[18:03:55.237] [INFO ] [Core] SIDERA Engine terminated with EXIT_SUCCESS.
```

---

## 10. Remaining Warnings

1. **`VK_LAYER_KHRONOS_validation` Unavailable on Host**: Reported honestly as `UNAVAILABLE` because the LunarG Vulkan SDK is not installed on the system.
2. **Global ReShade Implicit Layer (`ReShade64.dll` error 1114)**: Registered in `HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers`; self-unloads during `vkCreateInstance` because `sidera.exe` is not listed in `C:\ProgramData\ReShade\ReShadeApps.ini`.
3. **CMake `glm 1.0.1` Minimum Version Policy Warning**: Upstream `glm` `CMakeLists.txt` deprecation notice during CMake configuration.

---

## 11. Screenshot Location

- Project Documentation Copy: `docs/milestones/sidera_milestone04_vertex_index_buffers.png`
- Artifact Directory Copy: `C:\Users\ahmet\.gemini\antigravity\brain\0183e973-3dee-4195-85ab-648749e13d2f\sidera_milestone04_vertex_index_buffers.png`
