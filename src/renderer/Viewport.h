#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace sidera {

enum class ViewportMode : uint32_t {
    Lit = 0,              // A. Lit: Full PBR + Direct (CSM) + Point + IBL + Shadows + Textures
    Unlit = 1,            // B. Unlit: Base Color + Emissive without lighting
    Wireframe = 2,        // C. Wireframe: Mesh triangle edges via VK_POLYGON_MODE_LINE
    Solid = 3,            // D. Solid: Plain material colors with simple studio shading
    MaterialPreview = 4,  // E. Material Preview: PBR material with IBL environment only
    Normals = 5,          // F. Normals: Surface normals visualized as RGB (World or View space)
    Albedo = 6,           // G. Albedo: Base Color and albedo texture data
    Roughness = 7,        // H. Roughness: Roughness channel in grayscale
    Metallic = 8,         // I. Metallic: Metallic channel in grayscale
    AmbientOcclusion = 9, // J. Ambient Occlusion: AO channel in grayscale
    Depth = 10,           // K. Depth: Linearized scene depth visualization
    UV = 11,              // L. UV: UV coordinates calibration grid
    Emissive = 12,        // M. Emissive: Emissive texture and material data
    ShadowDebug = 13,     // N. Shadow Debug: Cascaded shadow map split overlay / debug
    Count = 14
};

[[nodiscard]] constexpr const char* getViewportModeName(ViewportMode mode) noexcept {
    switch (mode) {
        case ViewportMode::Lit:              return "Lit (PBR + IBL + CSM)";
        case ViewportMode::Unlit:            return "Unlit";
        case ViewportMode::Wireframe:        return "Wireframe";
        case ViewportMode::Solid:            return "Solid";
        case ViewportMode::MaterialPreview:  return "Material Preview (IBL Env)";
        case ViewportMode::Normals:          return "Normals";
        case ViewportMode::Albedo:           return "Albedo (Base Color)";
        case ViewportMode::Roughness:        return "Roughness";
        case ViewportMode::Metallic:         return "Metallic";
        case ViewportMode::AmbientOcclusion: return "Ambient Occlusion";
        case ViewportMode::Depth:            return "Depth (Linearized)";
        case ViewportMode::UV:               return "UV Coordinates";
        case ViewportMode::Emissive:         return "Emissive";
        case ViewportMode::ShadowDebug:      return "Shadow Debug (CSM Cascades)";
        default:                             return "Unknown";
    }
}

enum class ViewportLayout : uint32_t {
    Single = 0,           // Single fullscreen viewport
    SplitHorizontal = 1,  // Dual viewports: Left & Right
    SplitVertical = 2,    // Dual viewports: Top & Bottom
    Quad = 3              // 4 viewports (2x2 grid)
};

[[nodiscard]] constexpr const char* getViewportLayoutName(ViewportLayout layout) noexcept {
    switch (layout) {
        case ViewportLayout::Single:          return "Single Viewport";
        case ViewportLayout::SplitHorizontal: return "Split Horizontal (Dual)";
        case ViewportLayout::SplitVertical:   return "Split Vertical (Dual)";
        case ViewportLayout::Quad:            return "Quad View (4 Viewports)";
        default:                              return "Custom";
    }
}

struct Viewport {
    uint32_t id = 0;
    std::string name = "Main Viewport";
    ViewportMode mode = ViewportMode::Lit;
    uint32_t normalSpace = 0; // 0 = World space, 1 = View space

    // Normalized coordinates [0.0 .. 1.0]
    float relativeX = 0.0f;
    float relativeY = 0.0f;
    float relativeWidth = 1.0f;
    float relativeHeight = 1.0f;

    // Actual pixel viewport & scissor calculated for the current framebuffer extent
    VkViewport vkViewport{};
    VkRect2D vkScissor{};
};

} // namespace sidera
