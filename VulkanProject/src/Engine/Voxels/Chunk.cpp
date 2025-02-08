#include "Chunk.h"
#include <stdexcept>
#include <cstddef>

Chunk::Chunk(int worldX, int worldY, int worldZ)
    : m_worldX(worldX)
    , m_worldY(worldY)
    , m_worldZ(worldZ)
{
    // Allocate block data
    size_t total = static_cast<size_t>(SIZE_X)
        * static_cast<size_t>(SIZE_Y)
        * static_cast<size_t>(SIZE_Z);
    m_blocks.resize(total, 0); // zero => “Air”

    // By default, it's dirty => needs initial mesh
    m_dirty = true;
}

Chunk::~Chunk()
{
    // Typically we don't destroy chunk-level GPU buffers here 
    // (the manager or VoxelWorld does it before device destruction).
    // So we do nothing here.
}

int Chunk::getBlock(int x, int y, int z) const
{
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        return -1; // or “Air” ID
    }

    size_t idx = static_cast<size_t>(x)
        + static_cast<size_t>(SIZE_X) * (
            static_cast<size_t>(y)
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z));
    return m_blocks[idx];
}

void Chunk::setBlock(int x, int y, int z, int voxelID)
{
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        return; // out of bounds
    }

    size_t idx = static_cast<size_t>(x)
        + static_cast<size_t>(SIZE_X) * (
            static_cast<size_t>(y)
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z));

    int oldVal = m_blocks[idx];
    if (oldVal != voxelID) {
        m_blocks[idx] = voxelID;
        m_dirty = true; // Mark chunk as needing re-mesh
    }
}