#include "TerrainGenerator.h"
#include <chrono>
#include "../../Utils/CpuProfiler.h"

static double s_totalGenTime = 0.0;
static int    s_genCount = 0;

// -----------------------------------------------------------------------------
// Constructor Definition (Fixes LNK2001)
// -----------------------------------------------------------------------------
TerrainGenerator::TerrainGenerator()
{
    // Initialize noise parameters in the constructor
    m_seed = 1337;
    m_noise.SetSeed(m_seed);

    // Optionally tweak some default noise properties here
    m_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    m_noise.SetFrequency(0.01f);
    m_noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
    m_noise.SetFractalOctaves(4);
    m_noise.SetFractalLacunarity(2.0f);
    m_noise.SetFractalGain(0.5f);
}

// -----------------------------------------------------------------------------
// generateChunk
// -----------------------------------------------------------------------------
void TerrainGenerator::generateChunk(Chunk& chunk, int cx, int cy, int cz)
{
    CpuProfiler::ScopedTimer timer("TerrainGenerator::generateChunk");
    using namespace std::chrono;
    auto startTime = high_resolution_clock::now();

    int worldXOffset = cx * Chunk::SIZE_X;
    int worldYOffset = cy * Chunk::SIZE_Y;
    int worldZOffset = cz * Chunk::SIZE_Z;

    // Example: Larger base + amplitude for massive mountains
    int baseLevel = 8;
    int mountainAmp = 128;

    // Use a lower frequency => large horizontal features
    m_noise.SetFrequency(0.001f);

    // Fill chunk’s voxels
    for (int localX = 0; localX < Chunk::SIZE_X; localX++)
    {
        for (int localZ = 0; localZ < Chunk::SIZE_Z; localZ++)
        {
            float nVal = m_noise.GetNoise(
                float(worldXOffset + localX),
                float(worldZOffset + localZ)
            );
            float normalized = (nVal + 1.0f) * 0.5f;

            int terrainHeight = baseLevel + int(normalized * mountainAmp);

            for (int localY = 0; localY < Chunk::SIZE_Y; localY++)
            {
                int globalY = worldYOffset + localY;

                if (globalY <= terrainHeight)
                {
                    // surface logic
                    if (globalY == terrainHeight)
                        chunk.setBlock(localX, localY, localZ, 2); // Grass
                    else if (globalY >= terrainHeight - 2)
                        chunk.setBlock(localX, localY, localZ, 3); // Dirt
                    else
                        chunk.setBlock(localX, localY, localZ, 1); // Stone
                }
                else
                {
                    chunk.setBlock(localX, localY, localZ, 0); // Air
                }
            }
        }
    }

    // Detect if chunk is EMPTY, SOLID, or NORMAL
    int firstVal = chunk.getBlock(0, 0, 0);
    bool allSame = true;
    bool anyNonZero = (firstVal != 0);

    for (int z = 0; z < Chunk::SIZE_Z && allSame; z++)
    {
        for (int y = 0; y < Chunk::SIZE_Y && allSame; y++)
        {
            for (int x = 0; x < Chunk::SIZE_X; x++)
            {
                int val = chunk.getBlock(x, y, z);
                if (val != firstVal)
                {
                    allSame = false;
                    break;
                }
                if (val != 0) {
                    anyNonZero = true;
                }
            }
        }
    }

    if (allSame)
    {
        if (firstVal == 0)
            chunk.setState(Chunk::ChunkState::EMPTY);
        else
            chunk.setState(Chunk::ChunkState::SOLID);
    }
    else
    {
        chunk.setState(Chunk::ChunkState::NORMAL);
    }

    auto endTime = high_resolution_clock::now();
    s_totalGenTime += duration<double>(endTime - startTime).count();
    s_genCount++;
}

// -----------------------------------------------------------------------------
// getAvgGenTime
// -----------------------------------------------------------------------------
double TerrainGenerator::getAvgGenTime()
{
    if (s_genCount == 0) return 0.0;
    return s_totalGenTime / s_genCount;
}
