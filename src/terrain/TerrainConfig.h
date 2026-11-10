#pragma once

#include <cstdint>
#include <string>

namespace sidera {

struct TerrainConfig {
    float width = 120.0f;             // World-space size along X
    float length = 120.0f;            // World-space size along Z
    float heightScale = 14.0f;        // Max elevation range
    float heightOffset = -1.5f;       // Base elevation offset
    uint32_t chunksX = 4;             // Chunks along X axis (e.g. 4x4 = 16 chunks)
    uint32_t chunksZ = 4;             // Chunks along Z axis
    uint32_t chunkResolution = 48;    // Quads along each chunk edge (48x48 = 2,304 quads, 4,608 tris per chunk)
    float textureTiling = 24.0f;      // PBR detail texture tiling frequency
    std::string heightmapPath = "";   // Filepath for heightmap (PNG, TGA, BMP, HDR, etc.)
    std::string splatMapPath = "";    // Optional filepath for RGBA splat weight map
    uint32_t proceduralSeed = 1337;   // Seed for deterministic procedural generation
    uint32_t proceduralOctaves = 6;   // Octaves of simplex/FBM noise
    float proceduralFrequency = 1.0f; // Base spatial frequency
};

} // namespace sidera
