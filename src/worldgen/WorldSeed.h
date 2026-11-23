#pragma once

#include <cstdint>

namespace sidera {

// Pure, stateless, deterministic 64-bit seed derivation (SplitMix64 mixing).
// Guarantees identical results regardless of region generation order or thread scheduling.
class WorldSeed {
public:
    constexpr explicit WorldSeed(uint64_t seed = 1337ULL) noexcept : m_seed(seed) {}

    [[nodiscard]] constexpr uint64_t value() const noexcept { return m_seed; }

    [[nodiscard]] static constexpr uint64_t mix64(uint64_t z) noexcept {
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Derive an independent sub-seed for a specific generation layer (e.g. continental, mountain, climate)
    [[nodiscard]] constexpr WorldSeed deriveLayer(uint32_t layerId) const noexcept {
        return WorldSeed(mix64(m_seed ^ (static_cast<uint64_t>(layerId) * 0x9E3779B97F4A7C15ULL)));
    }

    // Derive a deterministic seed for a specific region (regionX, regionZ)
    [[nodiscard]] constexpr WorldSeed deriveRegion(int32_t regionX, int32_t regionZ) const noexcept {
        const uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(regionX)) << 32) |
                                static_cast<uint64_t>(static_cast<uint32_t>(regionZ));
        return WorldSeed(mix64(m_seed ^ mix64(packed)));
    }

    // Deterministic 2D integer lattice hash returning [0, 1)
    [[nodiscard]] constexpr float hash2DToFloat(int32_t x, int32_t z) const noexcept {
        const uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
                                static_cast<uint64_t>(static_cast<uint32_t>(z));
        const uint64_t h = mix64(m_seed ^ packed);
        return static_cast<float>((h >> 40) & 0xFFFFFFULL) * (1.0f / 16777216.0f);
    }

    [[nodiscard]] constexpr uint32_t toUint32() const noexcept {
        return static_cast<uint32_t>(m_seed ^ (m_seed >> 32));
    }

private:
    uint64_t m_seed = 1337ULL;
};

} // namespace sidera
