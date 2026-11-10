#include "foliage/ProceduralFoliage.h"
#include "renderer/Vertex.h"
#include "renderer/vulkan/VulkanDevice.h"

#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <numbers>
#include <vector>

namespace sidera {

// =============================================================================
// Procedural Tree Trunk Mesh (Cylindrical tapered trunk, root at local Y = 0)
// =============================================================================
std::unique_ptr<Mesh> ProceduralFoliage::createTreeTrunkMesh(
    uint32_t lodTier,
    float trunkHeight,
    float baseRadius,
    float tipRadius,
    std::string_view name
) {
    uint32_t radialSegments = 16;
    uint32_t heightSegments = 5;

    if (lodTier == 1) {
        radialSegments = 10;
        heightSegments = 3;
    } else if (lodTier == 2) {
        radialSegments = 6;
        heightSegments = 2;
    } else if (lodTier >= 3) {
        radialSegments = 4;
        heightSegments = 1;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const float twoPi = 2.0f * std::numbers::pi_v<float>;

    // Generate cylinder rings from y = 0 to y = trunkHeight
    for (uint32_t yIdx = 0; yIdx <= heightSegments; ++yIdx) {
        const float vCoord = static_cast<float>(yIdx) / static_cast<float>(heightSegments);
        const float y = vCoord * trunkHeight;
        const float radius = baseRadius * (1.0f - vCoord) + tipRadius * vCoord;

        // Wind flexibility: 0.0 at ground root, up to 0.25 at trunk crown
        const float windFlex = vCoord * 0.25f;
        const glm::vec3 barkColor(0.28f + vCoord * 0.05f, 0.18f + vCoord * 0.03f, 0.10f);

        for (uint32_t rIdx = 0; rIdx <= radialSegments; ++rIdx) {
            const float uCoord = static_cast<float>(rIdx) / static_cast<float>(radialSegments);
            const float angle = uCoord * twoPi;

            const float cosA = std::cos(angle);
            const float sinA = std::sin(angle);

            Vertex v{};
            v.position = glm::vec3(cosA * radius, y, sinA * radius);
            v.normal = glm::normalize(glm::vec3(cosA, 0.05f, sinA));
            v.uv = glm::vec2(uCoord * 2.0f, vCoord * 3.0f); // Tiling along trunk
            v.color = glm::vec3(windFlex, barkColor.y, barkColor.z); // color.r encodes wind weight

            vertices.push_back(v);
        }
    }

    // Cylinder side quads
    for (uint32_t yIdx = 0; yIdx < heightSegments; ++yIdx) {
        for (uint32_t rIdx = 0; rIdx < radialSegments; ++rIdx) {
            const uint32_t row1 = yIdx * (radialSegments + 1);
            const uint32_t row2 = (yIdx + 1) * (radialSegments + 1);

            indices.push_back(row1 + rIdx);
            indices.push_back(row2 + rIdx);
            indices.push_back(row1 + rIdx + 1);

            indices.push_back(row1 + rIdx + 1);
            indices.push_back(row2 + rIdx);
            indices.push_back(row2 + rIdx + 1);
        }
    }

    return std::make_unique<Mesh>(vertices, indices, name);
}

std::unique_ptr<Mesh> ProceduralFoliage::createTreeTrunkMesh(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    uint32_t lodTier,
    float trunkHeight,
    float baseRadius,
    float tipRadius,
    std::string_view name
) {
    const auto cpuMesh = createTreeTrunkMesh(lodTier, trunkHeight, baseRadius, tipRadius, name);
    return std::make_unique<Mesh>(device, transferCommandPool, cpuMesh->getCpuVertices(), cpuMesh->getCpuIndices(), name);
}

// =============================================================================
// Procedural Tree Canopy Mesh (Multi-tier conical / fir canopy with wind weights)
// =============================================================================
std::unique_ptr<Mesh> ProceduralFoliage::createTreeCanopyMesh(
    uint32_t lodTier,
    float canopyBaseHeight,
    float canopyTopHeight,
    float maxRadius,
    std::string_view name
) {
    uint32_t radialSegments = 24;
    uint32_t numTiers = 3;

    if (lodTier == 1) {
        radialSegments = 14;
        numTiers = 3;
    } else if (lodTier == 2) {
        radialSegments = 8;
        numTiers = 2;
    } else if (lodTier >= 3) {
        radialSegments = 5;
        numTiers = 2;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const float twoPi = 2.0f * std::numbers::pi_v<float>;
    const float tierSpan = (canopyTopHeight - canopyBaseHeight) / static_cast<float>(numTiers);

    for (uint32_t t = 0; t < numTiers; ++t) {
        const float tierFraction = static_cast<float>(t) / static_cast<float>(numTiers);
        const float tierBaseY = canopyBaseHeight + static_cast<float>(t) * tierSpan;
        const float tierTipY = tierBaseY + tierSpan * 1.55f;
        const float tierRadius = maxRadius * (1.0f - tierFraction * 0.28f);

        // Wind flexibility increases toward tree top and perimeter
        const float baseWindFlex = 0.35f + tierFraction * 0.40f;
        const float tipWindFlex = 0.55f + tierFraction * 0.45f;

        const uint32_t tipVertexIdx = static_cast<uint32_t>(vertices.size());
        Vertex tipV{};
        tipV.position = glm::vec3(0.0f, tierTipY, 0.0f);
        tipV.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        tipV.uv = glm::vec2(0.5f, 0.0f);
        tipV.color = glm::vec3(tipWindFlex, 0.55f, 0.18f); // color.r = wind weight
        vertices.push_back(tipV);

        const uint32_t ringStartIdx = static_cast<uint32_t>(vertices.size());
        for (uint32_t r = 0; r <= radialSegments; ++r) {
            const float uCoord = static_cast<float>(r) / static_cast<float>(radialSegments);
            const float angle = uCoord * twoPi;

            const float cosA = std::cos(angle);
            const float sinA = std::sin(angle);

            Vertex ringV{};
            ringV.position = glm::vec3(cosA * tierRadius, tierBaseY, sinA * tierRadius);
            ringV.normal = glm::normalize(glm::vec3(cosA, 0.35f, sinA));
            ringV.uv = glm::vec2(uCoord * 2.0f, 1.0f);
            ringV.color = glm::vec3(baseWindFlex, 0.48f, 0.15f); // color.r = wind weight
            vertices.push_back(ringV);
        }

        // Conical tier side triangles
        for (uint32_t r = 0; r < radialSegments; ++r) {
            indices.push_back(tipVertexIdx);
            indices.push_back(ringStartIdx + r + 1);
            indices.push_back(ringStartIdx + r);
        }

        // Bottom skirt under-cap for tier depth and self-shadowing
        const uint32_t underCenterIdx = static_cast<uint32_t>(vertices.size());
        Vertex underV{};
        underV.position = glm::vec3(0.0f, tierBaseY + 0.15f, 0.0f);
        underV.normal = glm::vec3(0.0f, -1.0f, 0.0f);
        underV.uv = glm::vec2(0.5f, 0.5f);
        underV.color = glm::vec3(baseWindFlex * 0.7f, 0.35f, 0.10f);
        vertices.push_back(underV);

        for (uint32_t r = 0; r < radialSegments; ++r) {
            indices.push_back(underCenterIdx);
            indices.push_back(ringStartIdx + r);
            indices.push_back(ringStartIdx + r + 1);
        }
    }

    return std::make_unique<Mesh>(vertices, indices, name);
}

std::unique_ptr<Mesh> ProceduralFoliage::createTreeCanopyMesh(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    uint32_t lodTier,
    float canopyBaseHeight,
    float canopyTopHeight,
    float maxRadius,
    std::string_view name
) {
    const auto cpuMesh = createTreeCanopyMesh(lodTier, canopyBaseHeight, canopyTopHeight, maxRadius, name);
    return std::make_unique<Mesh>(device, transferCommandPool, cpuMesh->getCpuVertices(), cpuMesh->getCpuIndices(), name);
}

// =============================================================================
// Procedural Grass Clump Mesh (Curved, tapered blades rooted at Y = 0)
// =============================================================================
std::unique_ptr<Mesh> ProceduralFoliage::createGrassClumpMesh(
    uint32_t bladeCount,
    float clumpRadius,
    float bladeHeight,
    std::string_view name
) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const float twoPi = 2.0f * std::numbers::pi_v<float>;
    const uint32_t verticalSegments = 4;

    for (uint32_t b = 0; b < bladeCount; ++b) {
        const float bladeFraction = static_cast<float>(b) / static_cast<float>(bladeCount);
        const float baseAngle = bladeFraction * twoPi + (static_cast<float>(b % 3) * 0.25f);

        // Root offset from center of clump
        const float rootOffset = (static_cast<float>((b * 13) % 7) / 7.0f) * clumpRadius;
        const glm::vec3 rootPos(std::cos(baseAngle) * rootOffset, 0.0f, std::sin(baseAngle) * rootOffset);

        // Blade outward curvature direction
        const glm::vec2 bendDir(std::cos(baseAngle), std::sin(baseAngle));
        const float bendAmount = 0.28f + (static_cast<float>(b % 4) * 0.06f);

        // Perpendicular vector for blade width
        const glm::vec2 perp(-bendDir.y, bendDir.x);
        const float baseHalfWidth = 0.055f;

        const uint32_t startIdx = static_cast<uint32_t>(vertices.size());

        for (uint32_t seg = 0; seg <= verticalSegments; ++seg) {
            const float t = static_cast<float>(seg) / static_cast<float>(verticalSegments);
            const float y = t * bladeHeight;

            // Outward quadratic parabolic bending
            const float horizontalDisplacement = t * t * bendAmount;
            const glm::vec3 center(
                rootPos.x + bendDir.x * horizontalDisplacement,
                y,
                rootPos.z + bendDir.y * horizontalDisplacement
            );

            // Width tapers to a sharp point at tip
            const float halfW = baseHalfWidth * (1.0f - t);

            // Wind weight: 0.0 at ground root (perfect grounding), 1.0 at blade tip
            const float windWeight = t * t;

            // Normal curves with the blade
            const glm::vec3 norm = glm::normalize(glm::vec3(bendDir.x * 0.3f, 1.0f - t * 0.5f, bendDir.y * 0.3f));

            // Left vertex
            Vertex vL{};
            vL.position = center - glm::vec3(perp.x * halfW, 0.0f, perp.y * halfW);
            vL.normal = norm;
            vL.uv = glm::vec2(0.0f, t);
            vL.color = glm::vec3(windWeight, 0.35f + t * 0.45f, 0.12f);
            vertices.push_back(vL);

            // Right vertex
            Vertex vR{};
            vR.position = center + glm::vec3(perp.x * halfW, 0.0f, perp.y * halfW);
            vR.normal = norm;
            vR.uv = glm::vec2(1.0f, t);
            vR.color = glm::vec3(windWeight, 0.35f + t * 0.45f, 0.12f);
            vertices.push_back(vR);
        }

        // Double-sided quad strip for each grass blade
        for (uint32_t seg = 0; seg < verticalSegments; ++seg) {
            const uint32_t i0 = startIdx + seg * 2;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + 2;
            const uint32_t i3 = i0 + 3;

            // Front face
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);

            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);

            // Back face
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            indices.push_back(i1);
            indices.push_back(i3);
            indices.push_back(i2);
        }
    }

    return std::make_unique<Mesh>(vertices, indices, name);
}

std::unique_ptr<Mesh> ProceduralFoliage::createGrassClumpMesh(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    uint32_t bladeCount,
    float clumpRadius,
    float bladeHeight,
    std::string_view name
) {
    const auto cpuMesh = createGrassClumpMesh(bladeCount, clumpRadius, bladeHeight, name);
    return std::make_unique<Mesh>(device, transferCommandPool, cpuMesh->getCpuVertices(), cpuMesh->getCpuIndices(), name);
}

// =============================================================================
// Procedural Bush Mesh (Clustered foliage domes, base at Y = 0)
// =============================================================================
std::unique_ptr<Mesh> ProceduralFoliage::createBushMesh(
    uint32_t lodTier,
    float radius,
    float height,
    std::string_view name
) {
    uint32_t radialSegments = 16;
    uint32_t elevationRings = 8;

    if (lodTier == 1) {
        radialSegments = 10;
        elevationRings = 5;
    } else if (lodTier == 2) {
        radialSegments = 7;
        elevationRings = 3;
    } else if (lodTier >= 3) {
        radialSegments = 5;
        elevationRings = 2;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const float twoPi = 2.0f * std::numbers::pi_v<float>;
    const float halfPi = std::numbers::pi_v<float> * 0.5f;

    // Hemispherical foliage dome with base anchored at local Y = 0
    for (uint32_t lat = 0; lat <= elevationRings; ++lat) {
        const float vCoord = static_cast<float>(lat) / static_cast<float>(elevationRings);
        const float phi = vCoord * halfPi; // 0 (horizon) to pi/2 (apex)

        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        const float ringY = sinPhi * height;
        const float ringR = cosPhi * radius;

        // Wind flexibility: 0.0 at ground, 0.7 at dome crown
        const float windFlex = vCoord * 0.70f;

        for (uint32_t lon = 0; lon <= radialSegments; ++lon) {
            const float uCoord = static_cast<float>(lon) / static_cast<float>(radialSegments);
            const float theta = uCoord * twoPi;

            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);

            Vertex v{};
            v.position = glm::vec3(cosTheta * ringR, ringY, sinTheta * ringR);
            v.normal = glm::normalize(glm::vec3(cosTheta * cosPhi, sinPhi * 1.2f, sinTheta * cosPhi));
            v.uv = glm::vec2(uCoord * 2.0f, vCoord * 2.0f);
            v.color = glm::vec3(windFlex, 0.42f, 0.14f); // color.r = wind weight
            vertices.push_back(v);
        }
    }

    for (uint32_t lat = 0; lat < elevationRings; ++lat) {
        for (uint32_t lon = 0; lon < radialSegments; ++lon) {
            const uint32_t current = lat * (radialSegments + 1) + lon;
            const uint32_t next = current + radialSegments + 1;

            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);

            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }

    return std::make_unique<Mesh>(vertices, indices, name);
}

std::unique_ptr<Mesh> ProceduralFoliage::createBushMesh(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    uint32_t lodTier,
    float radius,
    float height,
    std::string_view name
) {
    const auto cpuMesh = createBushMesh(lodTier, radius, height, name);
    return std::make_unique<Mesh>(device, transferCommandPool, cpuMesh->getCpuVertices(), cpuMesh->getCpuIndices(), name);
}

// =============================================================================
// Procedural Fern Fronds Mesh (Radial arching ground fronds rooted at Y = 0)
// =============================================================================
std::unique_ptr<Mesh> ProceduralFoliage::createFernMesh(
    uint32_t frondCount,
    float frondLength,
    std::string_view name
) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const float twoPi = 2.0f * std::numbers::pi_v<float>;
    const uint32_t segmentsPerFrond = 5;

    for (uint32_t f = 0; f < frondCount; ++f) {
        const float frondAngle = (static_cast<float>(f) / static_cast<float>(frondCount)) * twoPi;
        const glm::vec2 dir(std::cos(frondAngle), std::sin(frondAngle));
        const glm::vec2 perp(-dir.y, dir.x);

        const float halfWidth = 0.10f;
        const uint32_t startIdx = static_cast<uint32_t>(vertices.size());

        for (uint32_t seg = 0; seg <= segmentsPerFrond; ++seg) {
            const float t = static_cast<float>(seg) / static_cast<float>(segmentsPerFrond);
            const float dist = t * frondLength;

            // Natural arch: rises up to middle, then droops toward tip
            const float archY = std::sin(t * std::numbers::pi_v<float>) * 0.28f + (1.0f - t) * 0.05f;

            const glm::vec3 center(dir.x * dist, archY, dir.y * dist);
            const float w = halfWidth * std::sin(t * std::numbers::pi_v<float>); // Widest in middle
            const float windFlex = t * 0.85f;

            const glm::vec3 norm = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f) + glm::vec3(dir.x * 0.2f, 0.0f, dir.y * 0.2f));

            Vertex vL{};
            vL.position = center - glm::vec3(perp.x * w, 0.0f, perp.y * w);
            vL.normal = norm;
            vL.uv = glm::vec2(0.0f, t);
            vL.color = glm::vec3(windFlex, 0.50f, 0.16f);
            vertices.push_back(vL);

            Vertex vR{};
            vR.position = center + glm::vec3(perp.x * w, 0.0f, perp.y * w);
            vR.normal = norm;
            vR.uv = glm::vec2(1.0f, t);
            vR.color = glm::vec3(windFlex, 0.50f, 0.16f);
            vertices.push_back(vR);
        }

        // Double-sided frond quads
        for (uint32_t seg = 0; seg < segmentsPerFrond; ++seg) {
            const uint32_t i0 = startIdx + seg * 2;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + 2;
            const uint32_t i3 = i0 + 3;

            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);

            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);

            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            indices.push_back(i1);
            indices.push_back(i3);
            indices.push_back(i2);
        }
    }

    return std::make_unique<Mesh>(vertices, indices, name);
}

std::unique_ptr<Mesh> ProceduralFoliage::createFernMesh(
    const VulkanDevice& device,
    VkCommandPool transferCommandPool,
    uint32_t frondCount,
    float frondLength,
    std::string_view name
) {
    const auto cpuMesh = createFernMesh(frondCount, frondLength, name);
    return std::make_unique<Mesh>(device, transferCommandPool, cpuMesh->getCpuVertices(), cpuMesh->getCpuIndices(), name);
}

// =============================================================================
// Procedural Foliage Textures (Bark, Leaves, Grass with Alpha Cutouts)
// =============================================================================
void ProceduralFoliage::createFoliageTextures(TextureManager& textureManager) {
    constexpr uint32_t texSize = 256;

    // 1. Bark Albedo (Vertical wood grain / furrowed bark texture)
    std::vector<uint32_t> barkPixels(texSize * texSize);
    for (uint32_t y = 0; y < texSize; ++y) {
        for (uint32_t x = 0; x < texSize; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(texSize);
            const float fy = static_cast<float>(y) / static_cast<float>(texSize);

            // Vertical high-frequency grain with low-frequency bark ripple
            const float grain = std::sin(fx * 48.0f + std::sin(fy * 12.0f) * 3.0f);
            const float macro = std::sin(fx * 8.0f);
            const float val = std::clamp(0.45f + grain * 0.15f + macro * 0.10f, 0.0f, 1.0f);

            const uint8_t r = static_cast<uint8_t>(val * 95.0f);
            const uint8_t g = static_cast<uint8_t>(val * 65.0f);
            const uint8_t b = static_cast<uint8_t>(val * 40.0f);
            const uint8_t a = 255;

            barkPixels[y * texSize + x] = (static_cast<uint32_t>(a) << 24) |
                                          (static_cast<uint32_t>(b) << 16) |
                                          (static_cast<uint32_t>(g) << 8)  |
                                          static_cast<uint32_t>(r);
        }
    }
    textureManager.createTexture("foliage_bark_albedo", barkPixels.data(), texSize, texSize);

    // 2. Leaf Albedo with Alpha-Tested Cutout
    std::vector<uint32_t> leafPixels(texSize * texSize);
    for (uint32_t y = 0; y < texSize; ++y) {
        const float ny = (static_cast<float>(y) / static_cast<float>(texSize)) * 2.0f - 1.0f;
        for (uint32_t x = 0; x < texSize; ++x) {
            const float nx = (static_cast<float>(x) / static_cast<float>(texSize)) * 2.0f - 1.0f;

            // Heart / almond leaf contour
            const float d = std::sqrt(nx * nx + ny * ny);
            const float leafShape = 1.0f - d * 1.15f - std::abs(nx) * 0.35f;

            const bool isInside = (leafShape > 0.0f);
            const uint8_t a = isInside ? 255 : 0; // Crisp alpha-test binary cutout!

            const uint8_t r = isInside ? static_cast<uint8_t>(40.0f + (1.0f - d) * 35.0f) : 0;
            const uint8_t g = isInside ? static_cast<uint8_t>(115.0f + (1.0f - d) * 45.0f) : 0;
            const uint8_t b = isInside ? static_cast<uint8_t>(25.0f + (1.0f - d) * 20.0f) : 0;

            leafPixels[y * texSize + x] = (static_cast<uint32_t>(a) << 24) |
                                          (static_cast<uint32_t>(b) << 16) |
                                          (static_cast<uint32_t>(g) << 8)  |
                                          static_cast<uint32_t>(r);
        }
    }
    textureManager.createTexture("foliage_leaf_albedo", leafPixels.data(), texSize, texSize);

    // 3. Grass Albedo with Alpha-Tested Blade Cutout
    std::vector<uint32_t> grassPixels(texSize * texSize);
    for (uint32_t y = 0; y < texSize; ++y) {
        const float fy = static_cast<float>(y) / static_cast<float>(texSize); // 0 at root, 1 at tip
        for (uint32_t x = 0; x < texSize; ++x) {
            const float nx = (static_cast<float>(x) / static_cast<float>(texSize)) * 2.0f - 1.0f; // -1 to 1

            // Blade width narrows as y increases
            const float allowedWidth = (1.0f - fy * 0.85f) * 0.70f;
            const bool insideBlade = std::abs(nx) <= allowedWidth;
            const uint8_t a = insideBlade ? 255 : 0; // Cutout

            const uint8_t r = insideBlade ? static_cast<uint8_t>(30.0f + fy * 45.0f) : 0;
            const uint8_t g = insideBlade ? static_cast<uint8_t>(90.0f + fy * 85.0f) : 0;
            const uint8_t b = insideBlade ? static_cast<uint8_t>(15.0f + fy * 25.0f) : 0;

            grassPixels[y * texSize + x] = (static_cast<uint32_t>(a) << 24) |
                                           (static_cast<uint32_t>(b) << 16) |
                                           (static_cast<uint32_t>(g) << 8)  |
                                           static_cast<uint32_t>(r);
        }
    }
    textureManager.createTexture("foliage_grass_albedo", grassPixels.data(), texSize, texSize);
}

} // namespace sidera
