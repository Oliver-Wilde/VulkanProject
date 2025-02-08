// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "TerrainGenerator.h"
#include <cmath>

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
TerrainGenerator::TerrainGenerator()
{
    m_noise.SetSeed(m_seed);
    m_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_noise.SetFrequency(m_frequency);
    // Additional fractal parameters can be set here if desired.
}

// -----------------------------------------------------------------------------
// Public Methods
// -----------------------------------------------------------------------------
void TerrainGenerator::generateChunk(Chunk& chunk, int cx, int cy, int cz)
{
    // World offsets based on chunk coordinates
    int worldXOffset = cx * Chunk::SIZE_X;
    int worldZOffset = cz * Chunk::SIZE_Z;

    // Iterate over the local x,z coordinates in the chunk
    for (int localX = 0; localX < Chunk::SIZE_X; localX++)
    {
        for (int localZ = 0; localZ < Chunk::SIZE_Z; localZ++)
        {
            int worldX = worldXOffset + localX;
            int worldZ = worldZOffset + localZ;

            // Sample noise (range ~[-1..1])
            float nVal = m_noise.GetNoise(static_cast<float>(worldX), static_cast<float>(worldZ));
            // Convert to [0..1]
            float normalized = (nVal + 1.0f) * 0.5f;

            // Scale to a height value within [0..Chunk::SIZE_Y)
            int heightVal = static_cast<int>(normalized * (Chunk::SIZE_Y * 0.5f));
            if (heightVal < 0) heightVal = 0;
            if (heightVal >= Chunk::SIZE_Y) heightVal = Chunk::SIZE_Y - 1;

            // Fill from y=0 up to y=heightVal
            for (int y = 0; y <= heightVal; y++)
            {
                if (y == heightVal) {
                    // Top layer: grass
                    chunk.setBlock(localX, y, localZ, 3);
                }
                else if (y >= heightVal - 2) {
                    // Next two layers: dirt
                    chunk.setBlock(localX, y, localZ, 2);
                }
                else {
                    // Below: stone
                    chunk.setBlock(localX, y, localZ, 1);
                }
            }
        }
    }
}
