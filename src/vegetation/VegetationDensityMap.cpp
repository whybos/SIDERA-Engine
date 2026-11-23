#include "vegetation/VegetationDensityMap.h"
#include "worldgen/WorldGenerator.h"

#include <algorithm>
#include <cmath>

namespace sidera {

void VegetationDensityMap::generate(const WorldGenerator& worldGen,
                                    float minX, float maxX,
                                    float minZ, float maxZ,
                                    uint32_t resolution,
                                    float userDensityMultiplier) {
    m_resolution = std::max(resolution, 4u);
    m_minX = minX;
    m_maxX = maxX;
    m_minZ = minZ;
    m_maxZ = maxZ;
    m_values.resize(static_cast<size_t>(m_resolution) * m_resolution, 0.0f);

    const float invRes = 1.0f / static_cast<float>(m_resolution - 1u);
    for (uint32_t iz = 0; iz < m_resolution; ++iz) {
        const float wz = m_minZ + static_cast<float>(iz) * invRes * (m_maxZ - m_minZ);
        for (uint32_t ix = 0; ix < m_resolution; ++ix) {
            const float wx = m_minX + static_cast<float>(ix) * invRes * (m_maxX - m_minX);
            const WorldPointSample pt = worldGen.samplePoint(wx, wz);

            float d = 0.0f;
            if (pt.riverMask < 0.35f && pt.lakeMask < 0.35f) {
                // Moisture & moderate temperature boost density; steep slopes & extreme altitudes reduce it
                const float moistFactor = std::clamp(0.2f + pt.biome.moisture * 0.9f, 0.0f, 1.0f);
                const float tempFactor  = 1.0f - std::abs(pt.biome.temperature - 0.55f) * 1.1f;
                const float slopeFactor = std::clamp(1.0f - pt.biome.slope * 1.8f, 0.0f, 1.0f);
                const float altFactor   = std::clamp(1.0f - std::max(0.0f, pt.normalizedHeight - 0.65f) * 3.0f, 0.0f, 1.0f);
                const float riverBoost  = 1.0f + pt.riverMask * 0.35f;
                d = moistFactor * std::clamp(tempFactor, 0.1f, 1.0f) * slopeFactor * altFactor * riverBoost;
            }
            m_values[static_cast<size_t>(iz) * m_resolution + ix] = std::clamp(d * userDensityMultiplier, 0.0f, 1.0f);
        }
    }
}

float VegetationDensityMap::sample(float worldX, float worldZ) const noexcept {
    if (!isValid()) return 1.0f;
    const float u = std::clamp((worldX - m_minX) / std::max(m_maxX - m_minX, 1e-4f), 0.0f, 1.0f);
    const float v = std::clamp((worldZ - m_minZ) / std::max(m_maxZ - m_minZ, 1e-4f), 0.0f, 1.0f);

    const float px = u * static_cast<float>(m_resolution - 1u);
    const float pz = v * static_cast<float>(m_resolution - 1u);
    const uint32_t x0 = std::min(static_cast<uint32_t>(px), m_resolution - 1u);
    const uint32_t z0 = std::min(static_cast<uint32_t>(pz), m_resolution - 1u);
    const uint32_t x1 = std::min(x0 + 1u, m_resolution - 1u);
    const uint32_t z1 = std::min(z0 + 1u, m_resolution - 1u);
    const float fx = px - static_cast<float>(x0);
    const float fz = pz - static_cast<float>(z0);

    const float v00 = m_values[static_cast<size_t>(z0) * m_resolution + x0];
    const float v10 = m_values[static_cast<size_t>(z0) * m_resolution + x1];
    const float v01 = m_values[static_cast<size_t>(z1) * m_resolution + x0];
    const float v11 = m_values[static_cast<size_t>(z1) * m_resolution + x1];

    const float top = v00 + (v10 - v00) * fx;
    const float bot = v01 + (v11 - v01) * fx;
    return top + (bot - top) * fz;
}

} // namespace sidera
