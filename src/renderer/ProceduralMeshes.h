#pragma once

#include "renderer/Mesh.h"

#include <memory>
#include <string_view>

namespace sidera {

class VulkanDevice;

class ProceduralMeshes {
public:
    // A. High-resolution Icosphere with subdivision-based generation & smooth normals
    [[nodiscard]] static std::unique_ptr<Mesh> createIcosphere(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        float radius = 0.5f,
        uint32_t subdivisions = 5,
        std::string_view name = "Icosphere"
    );

    // B. Detailed Mechanical Gear with teeth, hub, bore, and beveled rims
    [[nodiscard]] static std::unique_ptr<Mesh> createGear(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        uint32_t numTeeth = 28,
        float outerRadius = 0.60f,
        float rootRadius = 0.46f,
        float boreRadius = 0.16f,
        float thickness = 0.18f,
        uint32_t radialRings = 12,
        std::string_view name = "MechanicalGear"
    );

    // C. High-polygon Torus with smooth normals & seamless UVs
    [[nodiscard]] static std::unique_ptr<Mesh> createTorus(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        float majorRadius = 0.50f,
        float minorRadius = 0.14f,
        uint32_t radialSegments = 128,
        uint32_t tubularSegments = 64,
        std::string_view name = "Torus"
    );

    // D. Procedural (p, q) Torus Knot with parallel-transport frame & smooth normals
    [[nodiscard]] static std::unique_ptr<Mesh> createTorusKnot(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        uint32_t p = 2,
        uint32_t q = 3,
        float radius = 0.40f,
        float tubeRadius = 0.09f,
        uint32_t curveSegments = 300,
        uint32_t tubeSegments = 64,
        std::string_view name = "TorusKnot"
    );

    // E. Detailed Multi-Part Mechanical Turbine Rotor with twisted airfoil blades
    [[nodiscard]] static std::unique_ptr<Mesh> createTurbine(
        const VulkanDevice& device,
        VkCommandPool transferCommandPool,
        uint32_t numBlades = 32,
        float hubRadius = 0.22f,
        float tipRadius = 0.65f,
        float hubLength = 0.35f,
        float bladeTwistDeg = 35.0f,
        uint32_t bladeSpanSegments = 16,
        uint32_t bladeChordSegments = 20,
        std::string_view name = "TurbineRotor"
    );
};

} // namespace sidera
