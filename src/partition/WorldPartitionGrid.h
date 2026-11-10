#pragma once

#include "renderer/BoundingVolume.h"

#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>
#include <functional>

namespace sidera {

// 2D discrete integer coordinates for partition grid cells
struct CellCoord {
    int32_t x = 0;
    int32_t z = 0;

    constexpr bool operator==(const CellCoord& other) const noexcept {
        return x == other.x && z == other.z;
    }

    constexpr bool operator!=(const CellCoord& other) const noexcept {
        return !(*this == other);
    }

    constexpr bool operator<(const CellCoord& other) const noexcept {
        if (x != other.x) return x < other.x;
        return z < other.z;
    }
};

} // namespace sidera

namespace std {
template <>
struct hash<sidera::CellCoord> {
    size_t operator()(const sidera::CellCoord& c) const noexcept {
        const uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(c.x));
        const uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(c.z));
        return static_cast<size_t>((ux << 32) | uz);
    }
};
} // namespace std

namespace sidera {

// Mathematical coordinate transforms, deterministic cell ID generation, and spatial indexing math
class WorldPartitionGrid {
public:
    // Stable 64-bit Cell ID bijection: preserves full range [-2^31, 2^31-1] without collisions
    [[nodiscard]] static constexpr uint64_t computeCellId(int32_t cx, int32_t cz) noexcept {
        const uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cx));
        const uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(cz));
        return (ux << 32) | uz;
    }

    // Inverts a stable 64-bit Cell ID back into exact discrete (x, z) coordinates
    [[nodiscard]] static constexpr CellCoord cellIdToCoord(uint64_t cellId) noexcept {
        const int32_t cx = static_cast<int32_t>(static_cast<uint32_t>(cellId >> 32));
        const int32_t cz = static_cast<int32_t>(static_cast<uint32_t>(cellId & 0xFFFFFFFFULL));
        return { cx, cz };
    }

    // World position -> Cell coordinate with floor semantics (safe for negative coordinates)
    [[nodiscard]] static CellCoord worldToCell(const glm::vec3& worldPos,
                                               float cellSize,
                                               const glm::vec3& origin = glm::vec3(0.0f)) noexcept {
        const float relX = worldPos.x - origin.x;
        const float relZ = worldPos.z - origin.z;
        const int32_t cx = static_cast<int32_t>(std::floor(relX / cellSize));
        const int32_t cz = static_cast<int32_t>(std::floor(relZ / cellSize));
        return { cx, cz };
    }

    // Cell coordinate -> World bounds
    [[nodiscard]] static AABB cellToWorldBounds(const CellCoord& coord,
                                                float cellSize,
                                                const glm::vec3& origin = glm::vec3(0.0f),
                                                float minY = -200.0f,
                                                float maxY = 600.0f) noexcept {
        const float minX = origin.x + static_cast<float>(coord.x) * cellSize;
        const float minZ = origin.z + static_cast<float>(coord.z) * cellSize;
        return AABB{
            .min = glm::vec3(minX, minY, minZ),
            .max = glm::vec3(minX + cellSize, maxY, minZ + cellSize)
        };
    }

    // Cell coordinate -> Macro cell coordinate (for hierarchical macro regions)
    [[nodiscard]] static CellCoord cellToMacroCoord(const CellCoord& coord, uint32_t macroFactor) noexcept {
        const float mf = static_cast<float>(macroFactor > 0 ? macroFactor : 1);
        const int32_t mx = static_cast<int32_t>(std::floor(static_cast<float>(coord.x) / mf));
        const int32_t mz = static_cast<int32_t>(std::floor(static_cast<float>(coord.z) / mf));
        return { mx, mz };
    }

    // Macro cell coordinate -> World bounds
    [[nodiscard]] static AABB macroToWorldBounds(const CellCoord& macroCoord,
                                                 float cellSize,
                                                 uint32_t macroFactor,
                                                 const glm::vec3& origin = glm::vec3(0.0f),
                                                 float minY = -200.0f,
                                                 float maxY = 600.0f) noexcept {
        const float macroSize = cellSize * static_cast<float>(macroFactor > 0 ? macroFactor : 1);
        const float minX = origin.x + static_cast<float>(macroCoord.x) * macroSize;
        const float minZ = origin.z + static_cast<float>(macroCoord.z) * macroSize;
        return AABB{
            .min = glm::vec3(minX, minY, minZ),
            .max = glm::vec3(minX + macroSize, maxY, minZ + macroSize)
        };
    }

    // Computes the inclusive range of cell coordinates intersecting an AABB
    static void getIntersectingCellRange(const AABB& aabb,
                                         float cellSize,
                                         const glm::vec3& origin,
                                         CellCoord& outMin,
                                         CellCoord& outMax) noexcept {
        outMin = worldToCell(aabb.min, cellSize, origin);
        outMax = worldToCell(aabb.max, cellSize, origin);
    }
};

} // namespace sidera
