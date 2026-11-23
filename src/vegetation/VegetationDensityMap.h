#pragma once

#include <cstdint>
#include <vector>

namespace sidera {

class WorldGenerator;

// CPU/GPU-compatible 2D vegetation density map derived from biome, climate, slope, altitude, and water masks.
class VegetationDensityMap {
public:
    VegetationDensityMap() = default;

    void generate(const WorldGenerator& worldGen,
                  float minX, float maxX,
                  float minZ, float maxZ,
                  uint32_t resolution = 64,
                  float userDensityMultiplier = 1.0f);

    [[nodiscard]] float sample(float worldX, float worldZ) const noexcept;
    [[nodiscard]] bool isValid() const noexcept { return !m_values.empty() && m_resolution >= 2; }
    [[nodiscard]] uint32_t getResolution() const noexcept { return m_resolution; }
    [[nodiscard]] const std::vector<float>& getRawData() const noexcept { return m_values; }
    [[nodiscard]] uint64_t getMemoryBytes() const noexcept { return m_values.size() * sizeof(float); }

private:
    std::vector<float> m_values;
    uint32_t m_resolution = 0;
    float m_minX = 0.0f, m_maxX = 1.0f;
    float m_minZ = 0.0f, m_maxZ = 1.0f;
};

} // namespace sidera
