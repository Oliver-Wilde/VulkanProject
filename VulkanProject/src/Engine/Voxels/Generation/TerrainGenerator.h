#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "Engine/Voxels/Chunk.h"
#include "FastNoiseLite.h" // Single-header library

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class TerrainGenerator
{
public:
    // -----------------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------------
    TerrainGenerator();
    ~TerrainGenerator() = default;

    // -----------------------------------------------------------------------------
    // Public Methods
    // -----------------------------------------------------------------------------
    /**
     * Generates voxel data for the given chunk at chunk coordinates (cx, cy, cz).
     * This uses a simple heightmap-based approach to populate the chunk with terrain.
     *
     * @param chunk Reference to the Chunk to be populated with blocks.
     * @param cx    Chunk X coordinate (in chunk-space).
     * @param cy    Chunk Y coordinate (unused in basic heightmap).
     * @param cz    Chunk Z coordinate (in chunk-space).
     */
    void generateChunk(Chunk& chunk, int cx, int cy, int cz);
    static double getAvgGenTime(); // <-- Add this

private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    FastNoiseLite m_noise;    ///< The noise generator used for creating terrain.
    float         m_frequency = 0.005f; ///< Frequency for the noise function.
    int           m_seed = 1337;  ///< Seed for the noise generator.
};
