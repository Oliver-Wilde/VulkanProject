#include "Chunk.h"
#include <stdexcept>
#include <cstddef>       // for size_t
#include <glm/vec3.hpp>  // for glm::vec3
#include <utility>       // for std::pair

Chunk::Chunk(int worldX, int worldY, int worldZ)
    : m_worldX(worldX)
    , m_worldY(worldY)
    , m_worldZ(worldZ)
{
    // Allocate block data: 16x16x16 by default => 4096 ints
    size_t total = static_cast<size_t>(SIZE_X)
        * static_cast<size_t>(SIZE_Y)
        * static_cast<size_t>(SIZE_Z);
    m_blocks.resize(total, 0); // 0 => "Air"

    // Initially, mark all LODs dirty (true). 
    // Already done in the member initializer list: {true,true,true}
    // so there's no extra code needed here for LOD dirty flags.
}

Chunk::~Chunk()
{
    // Typically, GPU buffer destruction is done elsewhere (e.g. VoxelWorld)
    // so no special cleanup here. 
    // (But if desired, you could do it here.)
}

int Chunk::getBlock(int x, int y, int z) const
{
    // Bounds check
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        // Return -1 if out-of-bounds => treat as air
        return -1;
    }

    // Flatten (x,y,z) => index
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
        return; // out of range => do nothing
    }

    // Flatten index
    size_t idx = static_cast<size_t>(x)
        + static_cast<size_t>(SIZE_X) * (
            static_cast<size_t>(y)
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z)
            );

    int oldVal = m_blocks[idx];
    if (oldVal != voxelID) {
        // Update the block
        m_blocks[idx] = voxelID;

        // Mark *all* LODs dirty, because the chunk changed
        markAllLODsDirty();
    }
}

void Chunk::markAllLODsDirty()
{
    // Mark every LOD level as dirty
    for (int level = 0; level < MAX_LOD_LEVELS; level++) {
        m_lodDirty[level] = true;
    }
}

void Chunk::getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const
{
    float chunkOriginX = static_cast<float>(m_worldX * SIZE_X);
    float chunkOriginY = static_cast<float>(m_worldY * SIZE_Y);
    float chunkOriginZ = static_cast<float>(m_worldZ * SIZE_Z);

    outMin = glm::vec3(chunkOriginX, chunkOriginY, chunkOriginZ);
    outMax = outMin + glm::vec3(SIZE_X, SIZE_Y, SIZE_Z);
}

std::pair<size_t, size_t> Chunk::getVoxelUsage() const
{
    size_t activeCount = 0;
    size_t emptyCount = 0;

    for (int voxel : m_blocks)
    {
        if (voxel == 0)  // air
            emptyCount++;
        else
            activeCount++;
    }
    return { activeCount, emptyCount };
}
