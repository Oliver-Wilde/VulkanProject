#include "Chunk.h"
#include <stdexcept>
#include <cstddef>       // for size_t
#include <glm/vec3.hpp>
#include <utility>       // for std::pair

Chunk::Chunk(int worldX, int worldY, int worldZ)
    : m_worldX(worldX)
    , m_worldY(worldY)
    , m_worldZ(worldZ)
{
    // Allocate block data: 16x16x16 => 4096 ints, default to 0 (air)
    size_t total = static_cast<size_t>(SIZE_X)
        * static_cast<size_t>(SIZE_Y)
        * static_cast<size_t>(SIZE_Z);
    m_blocks.resize(total, 0); // 0 => "Air"

    // By default, LOD dirty flags are set to true in the initializer list.
    // Seam data is also defaulted to invalid. No special code needed here.
}

Chunk::~Chunk()
{
    // Typically, GPU buffer destruction is done externally (e.g. VoxelWorld).
    // If you wanted to unify that destruction here, you could do it, but 
    // it’s more common to let the manager or VoxelWorld handle it.
}

int Chunk::getBlock(int x, int y, int z) const
{
    // Out-of-bounds => treat as air
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        return -1;
    }

    // Flatten index (x + SIZE_X*(y + SIZE_Y*z))
    size_t idx = static_cast<size_t>(x)
        + static_cast<size_t>(SIZE_X) * (
            static_cast<size_t>(y)
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z)
            );
    return m_blocks[idx];
}

void Chunk::setBlock(int x, int y, int z, int voxelID)
{
    // Out-of-bounds => do nothing
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        return;
    }

    // Flatten index
    size_t idx = static_cast<size_t>(x)
        + static_cast<size_t>(SIZE_X) * (
            static_cast<size_t>(y)
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z)
            );

    int oldVal = m_blocks[idx];
    if (oldVal != voxelID)
    {
        m_blocks[idx] = voxelID;
        // Mark all LOD levels dirty
        markAllLODsDirty();
        // Potentially mark all seams dirty as well, since block changes
        // might affect boundary transitions. That’s optional:
        // markAllSeamsDirty();
    }
}

void Chunk::markAllLODsDirty()
{
    for (int level = 0; level < MAX_LOD_LEVELS; level++) {
        m_lodDirty[level] = true;
    }
}

void Chunk::getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const
{
    float chunkOriginX = float(m_worldX * SIZE_X);
    float chunkOriginY = float(m_worldY * SIZE_Y);
    float chunkOriginZ = float(m_worldZ * SIZE_Z);

    outMin = glm::vec3(chunkOriginX, chunkOriginY, chunkOriginZ);
    outMax = outMin + glm::vec3(SIZE_X, SIZE_Y, SIZE_Z);
}

std::pair<size_t, size_t> Chunk::getVoxelUsage() const
{
    size_t activeCount = 0;
    size_t emptyCount = 0;

    for (int voxel : m_blocks)
    {
        if (voxel == 0)  // 0 => air
            emptyCount++;
        else
            activeCount++;
    }
    return { activeCount, emptyCount };
}
