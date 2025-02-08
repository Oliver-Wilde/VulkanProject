// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "Chunk.h"
#include <stdexcept>
#include <cstddef>         // for size_t
#include <glm/vec3.hpp>    // for glm::vec3

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

    // By default, it's dirty => needs initial mesh
    m_dirty = true;
}

Chunk::~Chunk()
{
    // Typically we don't destroy the chunk's GPU buffers here.
    // The manager or VoxelWorld might handle that before device destruction.
    // So do nothing here.
}

// -----------------------------------------------------------------------------
// Block Access
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
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z));
    return m_blocks[idx];
}

void Chunk::setBlock(int x, int y, int z, int voxelID)
{
    // Bounds check
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        return; // out of bounds
    }

    // Calculate 1D index
    size_t idx = static_cast<size_t>(x)
        + static_cast<size_t>(SIZE_X) * (
            static_cast<size_t>(y)
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z));

    int oldVal = m_blocks[idx];
    if (oldVal != voxelID) {
        // Update the block ID
        m_blocks[idx] = voxelID;
        // Mark chunk as needing re-mesh
        m_dirty = true;
    }
}

// -----------------------------------------------------------------------------
// getBoundingBox
// -----------------------------------------------------------------------------
void Chunk::getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const
{
    // Calculate the world-space offset of this chunk
    float chunkOriginX = static_cast<float>(m_worldX * SIZE_X);
    float chunkOriginY = static_cast<float>(m_worldY * SIZE_Y);
    float chunkOriginZ = static_cast<float>(m_worldZ * SIZE_Z);

    // Minimum corner is the chunk origin
    outMin = glm::vec3(chunkOriginX, chunkOriginY, chunkOriginZ);

    // Maximum corner is origin + (SIZE_X, SIZE_Y, SIZE_Z)
    outMax = outMin + glm::vec3(SIZE_X, SIZE_Y, SIZE_Z);
}
