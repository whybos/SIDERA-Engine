# SIDERA Engine v0.7.0 — Milestone 07: Depth Buffer

**Status**: COMPLETE  
**Target GPU**: NVIDIA GeForce RTX 4060 Laptop GPU (Vulkan 1.4.341 / Engine API 1.3)

## 1. Summary of Changes
- **Dedicated Depth Attachment (`sidera::VulkanDepthBuffer`)**:
  - Implemented `src/renderer/vulkan/VulkanDepthBuffer.h` and `src/renderer/vulkan/VulkanDepthBuffer.cpp`.
  - Queries physical device format properties for optimal-tiling depth support (`VK_FORMAT_D32_SFLOAT`, `VK_FORMAT_D32_SFLOAT_S8_UINT`, `VK_FORMAT_D24_UNORM_S8_UINT`), selecting `VK_FORMAT_D32_SFLOAT`.
  - Allocates a 2D `VkImage` (`VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`), binds `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` `VkDeviceMemory`, and creates a `VkImageView` (`VK_IMAGE_ASPECT_DEPTH_BIT`).
  - Supports `recreate(uint32_t width, uint32_t height)` to automatically match swapchain dimensions across window resize and minimize/restore events.
- **Pipeline Depth-Stencil State (`sidera::GraphicsPipeline`)**:
  - Enabled `VkPipelineDepthStencilStateCreateInfo` with `depthTestEnable = VK_TRUE`, `depthWriteEnable = VK_TRUE`, and `depthCompareOp = VK_COMPARE_OP_LESS`.
  - Configured `VkPipelineRenderingCreateInfo::depthAttachmentFormat` for Vulkan 1.3 Dynamic Rendering.
- **Dynamic Rendering & Synchronization2 Integration (`sidera::Renderer`)**:
  - Added `VK_IMAGE_LAYOUT_UNDEFINED -> VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL` transition via `vkCmdPipelineBarrier2` (`VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT`).
  - Configured `VkRenderingAttachmentInfo` for depth (`VK_ATTACHMENT_LOAD_OP_CLEAR` to `1.0f`, `VK_ATTACHMENT_STORE_OP_DONT_CARE`) and attached it to `VkRenderingInfo::pDepthAttachment`.
  - Arranged 5 scene objects in front-to-back draw order (with the ground plane drawn last and a spire pyramid intersecting through the center cube) to visually and mathematically prove per-pixel hardware depth testing.

## 2. Verification Results
- **Build**: Succeeded with zero warnings/errors (`MSVC 19.44.35213.0`).
- **Runtime Smoke Test**:
  - Verified depth buffer creation (`1280x720`, `VK_FORMAT_D32_SFLOAT`), dynamic resize (`1024x576 -> 1280x720`), minimize/restore, 120 rendered frames, and clean RAII destruction.
- **Visual Screenshot**: Saved to `docs/milestones/sidera_milestone07_depth_buffer.png`.
