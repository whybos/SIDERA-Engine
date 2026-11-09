# SIDERA Engine v0.5.0 — Milestone 05: 3D Camera System

## 1. Architecture Changes

Milestone 05 introduces a reusable 3D perspective camera (`sidera::Camera`), keyboard/mouse input polling (`sidera::Window`), per-frame Uniform Buffers (`CameraUniformData`), and Vulkan descriptor set management (`VkDescriptorSetLayout`, `VkDescriptorPool`, `VkDescriptorSet`).

- **`sidera::Camera` (`src/renderer/Camera.h` / `Camera.cpp`)**:
  - Maintains world position, Euler angles (`yawDegrees`, `pitchDegrees` clamped to `[-89.0, +89.0]` degrees), orthonormal basis vectors (`forward`, `right`, `up`), field of view (`fovDegrees = 60.0`), near/far clipping planes (`0.1` to `200.0`), and aspect ratio.
  - Computes right-handed view matrix (`glm::lookAt`) and Vulkan clip-space perspective projection matrix (`glm::perspective` with `GLM_FORCE_DEPTH_ZERO_TO_ONE` and `proj[1][1] *= -1.0f`).
  - Processes WASD + E/Q keyboard translation and Right-Mouse-Button mouse look with configurable movement speed (`3.5 units/s`) and mouse sensitivity (`0.12 deg/px`).
- **Descriptor & Uniform Buffer Pipeline (`src/renderer/Renderer.cpp`)**:
  - Allocates one `HOST_VISIBLE | HOST_COHERENT` `VulkanBuffer` (`192 bytes`, `VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT`) and one `VkDescriptorSet` per frame in flight (`kMaxFramesInFlight = 2`), bound at `set = 0, binding = 0`.
  - Automatically updates camera aspect ratio during swapchain recreation (`Renderer::recreateSwapchain`).

## 2. Files Created & Modified

### Created
- `src/renderer/Camera.h` & `src/renderer/Camera.cpp`
- `docs/milestones/milestone_05_3d_camera.md`
- `docs/milestones/sidera_milestone05_3d_camera.png`

### Modified
- `src/core/Window.h` & `src/core/Window.cpp` — Added keyboard (`isKeyPressed`), mouse button (`isMouseButtonPressed`), cursor position (`getCursorPosition`), and cursor capture (`setCursorCaptured`) APIs.
- `shaders/triangle.vert` — Added `layout(set = 0, binding = 0) uniform CameraUBO` and transformed 3D vertices via `ubo.viewProj * vec4(inPosition, 1.0)`.
- `src/renderer/vulkan/GraphicsPipeline.h` & `src/renderer/vulkan/GraphicsPipeline.cpp` — Bound `VkDescriptorSetLayout` into `VkPipelineLayoutCreateInfo`.
- `src/renderer/Renderer.h` & `src/renderer/Renderer.cpp` — Integrated `Camera`, per-frame uniform buffers, descriptor sets, and a 3D multi-triangle test scene (`10 vertices, 12 indices`).
- `src/core/Application.h`, `src/core/Application.cpp`, `src/main.cpp`, `src/renderer/vulkan/VulkanInstance.cpp`, & `CMakeLists.txt` — Updated to `v0.5.0` and added automated 3D camera verification tests.

## 3. Build & Runtime Verification

- **Compiler**: MSVC `19.44.35213.0` (`C++20`, `/W4 /permissive-`) — **0 errors, 0 warnings**
- **Verified Camera Operations**:
  1. Initial orientation (`pos=[1.65, 1.20, 2.35], yaw=-125.0 deg, pitch=-22.0 deg, FOV=60.0 deg, aspect=1.778`).
  2. Forward/backward & left/right movement (`pos=[1.16, 1.03, 2.18]` -> restored to `[1.65, 1.20, 2.35]`).
  3. Mouse look rotation (`yaw=-110.0 deg, pitch=-12.0 deg`).
  4. Pitch clamping (`maxPitch=89.0 deg, minPitch=-89.0 deg`).
  5. Aspect ratio updates across resize (`1024x576` -> `1280x720`) and minimize/restore.
  6. Clean reverse-order shutdown.

## 4. Screenshot Location
- `docs/milestones/sidera_milestone05_3d_camera.png`
