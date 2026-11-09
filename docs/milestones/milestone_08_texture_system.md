# SIDERA Engine v0.8.0 — Milestone 08: Texture System

**Status**: COMPLETE (RTX 4060 Laptop GPU, Vulkan 1.3)

## Changes
- `Vertex` now has `vec2 uv` (location 2, `R32G32_SFLOAT`, stride 32 B); UVs added to cube, pyramid and ground grid.
- `VulkanTexture` (RAII): `VkImage` (R8G8B8A8_SRGB, DEVICE_LOCAL), `VkImageView`, `VkSampler` (linear, repeat, anisotropy 16x when supported).
- Upload via staging `VulkanBuffer` + `vkCmdCopyBufferToImage` with `vkCmdPipelineBarrier2` transitions (UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY).
- `VulkanDevice` queries and enables `samplerAnisotropy` when available.
- Descriptor set 0: binding 0 camera UBO (vertex), binding 1 `COMBINED_IMAGE_SAMPLER` (fragment). Shaders sample `uAlbedoMap` and modulate vertex color.
- Procedural 256x256 UV grid/ring test texture (no external asset loading yet).

## Verification
- Build succeeded; `--smoke-test` passed (120 frames, resize, minimize/restore, clean shutdown, `SamplerAnisotropy: ENABLED`).
- Screenshot: `sidera_milestone08_texture_system.png` shows textured cube, pyramids and ground.
- Validation layer still unavailable on host (not installed); no validation claims made.
