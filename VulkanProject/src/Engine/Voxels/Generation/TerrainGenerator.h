#pragma once
#include "Engine/Voxels/Chunk.h"
#include "FastNoiseLite.h" // single-header

class TerrainGenerator
{
public:
    TerrainGenerator();
    ~TerrainGenerator() = default;

    // Basic heightmap approach
    void generateChunk(Chunk& chunk, int cx, int cy, int cz);

private:
    FastNoiseLite m_noise;
    float         m_frequency = 0.01f;
    int           m_seed = 1337; // or random
};