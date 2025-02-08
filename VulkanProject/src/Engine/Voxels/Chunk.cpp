// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "Chunk.h"
#include <stdexcept>
#include <cstddef>

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------
Chunk::Chunk(int worldX, int worldY, int worldZ)
    : m_worldX(worldX)
    , m_worldY(worldY)
    , m_worldZ(worldZ)
{
    // Allocate block data
    size_t total = static_cast<size_t>(SIZE_X)
        * static_cast<size_t>(SIZE_Y)
        * static_cast<size_t>(SIZE_Z);
    m_blocks.resize(total, 0); // 0 => "Air"

    // By default, it's dirty => needs initial mesh creation
    m_dirty = true;
}

Chunk::~Chunk()
{
    // Typically we don't destroy chunk-level GPU buffers here.
    // The manager or VoxelWorld might handle that before device destruction.
    // So we do nothing here.
}

// -----------------------------------------------------------------------------
// Public Methods
// -----------------------------------------------------------------------------
int Chunk::getBlock(int x, int y, int z) const
{
    // Bounds check
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        // Return -1 or "Air" ID to indicate out-of-bounds
        return -1;
    }

    // Calculate 1D index
    size_t idx = static_cast<size_t>(x)
        + static_cast<size_t>(SIZE_X) * (
            static_cast<size_t>(y)
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z)
            );
    return m_blocks[idx];
}

void Chunk::setBlock(int x, int y, int z, int voxelID)
{
    // Bounds check
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        return; // Out of bounds
    }

    // Calculate 1D index
    size_t idx = static_cast<size_t>(x)
        + static_cast<size_t>(SIZE_X) * (
            static_cast<size_t>(y)
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z)
            );

    int oldVal = m_blocks[idx];
    if (oldVal != voxelID) {
        // Update block ID
        m_blocks[idx] = voxelID;
        // Mark chunk as needing re-mesh
        m_dirty = true;
    }
}
