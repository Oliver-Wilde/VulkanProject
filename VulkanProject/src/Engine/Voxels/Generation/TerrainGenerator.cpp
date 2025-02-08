
#include "TerrainGenerator.h"
#include <cmath>

TerrainGenerator::TerrainGenerator()
{
    m_noise.SetSeed(m_seed);
    m_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_noise.SetFrequency(m_frequency);
    // Could set fractal parameters, etc.
}

void TerrainGenerator::generateChunk(Chunk& chunk, int cx, int cy, int cz)
{
    int worldXOffset = cx * Chunk::SIZE_X;
    int worldZOffset = cz * Chunk::SIZE_Z;

    for (int localX = 0; localX < Chunk::SIZE_X; localX++)
    {
        for (int localZ = 0; localZ < Chunk::SIZE_Z; localZ++)
        {
            int worldX = worldXOffset + localX;
            int worldZ = worldZOffset + localZ;

            float nVal = m_noise.GetNoise((float)worldX, (float)worldZ); // ~[-1..1]
            float normalized = (nVal + 1.f) * 0.5f;                       // [0..1]

            int heightVal = (int)(normalized * (Chunk::SIZE_Y * 0.5f));
            if (heightVal < 0) heightVal = 0;
            if (heightVal >= Chunk::SIZE_Y) heightVal = Chunk::SIZE_Y - 1;

            // Fill y=0 .. y=heightVal
            for (int y = 0; y <= heightVal; y++)
            {
                if (y == heightVal) {
                    chunk.setBlock(localX, y, localZ, 3); // grass
                }
                else if (y >= heightVal - 2) {
                    chunk.setBlock(localX, y, localZ, 2); // dirt
                }
                else {
                    chunk.setBlock(localX, y, localZ, 1); // stone
                }
            }
        }
    }
}