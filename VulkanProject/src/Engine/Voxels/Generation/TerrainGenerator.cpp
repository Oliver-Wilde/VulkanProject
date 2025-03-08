#include "TerrainGenerator.h"
#include <chrono>
#include "../../Utils/CpuProfiler.h"

static double s_totalGenTime = 0.0;
static int    s_genCount = 0;

TerrainGenerator::TerrainGenerator()
{
    m_seed = 1337;
    m_noise.SetSeed(m_seed);

    // Keep ridged for interesting spiky terrain,
    // but smaller amplitude => ground won't go super high
    m_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    m_noise.SetFrequency(0.005f);
    m_noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
    m_noise.SetFractalOctaves(4);
    m_noise.SetFractalLacunarity(2.0f);
    m_noise.SetFractalGain(0.5f);
}

void TerrainGenerator::generateChunk(Chunk& chunk, int cx, int cy, int cz)
{
    CpuProfiler::ScopedTimer timer("TerrainGenerator::generateChunk");
    using namespace std::chrono;
    auto startTime = high_resolution_clock::now();

    int worldXOffset = cx * Chunk::SIZE_X;
    int worldYOffset = cy * Chunk::SIZE_Y;
    int worldZOffset = cz * Chunk::SIZE_Z;

    // Lower base + amplitude for smaller worlds
    int baseLevel = 2;
    int mountainAmp = 64;

    for (int localX = 0; localX < Chunk::SIZE_X; localX++)
    {
        for (int localZ = 0; localZ < Chunk::SIZE_Z; localZ++)
        {
            float nVal = m_noise.GetNoise(
                float(worldXOffset + localX),
                float(worldZOffset + localZ)
            );
            float normalized = (nVal + 1.0f) * 0.5f;

            // terrain up to ~ Y=(baseLevel+mountainAmp)=8+24=32 at most
            int terrainHeight = baseLevel + int(normalized * mountainAmp);

            for (int localY = 0; localY < Chunk::SIZE_Y; localY++)
            {
                int globalY = worldYOffset + localY;

                if (globalY <= terrainHeight)
                {
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

    auto endTime = high_resolution_clock::now();
    s_totalGenTime += duration<double>(endTime - startTime).count();
    s_genCount++;
}

double TerrainGenerator::getAvgGenTime()
{
    if (s_genCount == 0) return 0.0;
    return s_totalGenTime / s_genCount;
}
