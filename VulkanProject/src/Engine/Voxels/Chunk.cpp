#include "Chunk.h"
#include <stdexcept>  // For runtime_error
#include <cstddef>    // For size_t
#include <glm/vec3.hpp>
#include <utility>    // For std::pair

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------
Chunk::Chunk(int worldX, int worldY, int worldZ)
    : m_worldX(worldX)
    , m_worldY(worldY)
    , m_worldZ(worldZ)
    , m_dirty(true)
    , m_isUploading(false)
    , m_state(ChunkState::NORMAL)
{
    // Allocate the voxel array
    size_t total = size_t(SIZE_X) * size_t(SIZE_Y) * size_t(SIZE_Z);
    m_blocks.resize(total, 0); // 0 => "Air"
}

Chunk::~Chunk()
{
    // Typically we don't destroy GPU buffers here;
    // that is handled by VoxelWorld or ResourceManager.
}

// -----------------------------------------------------------------------------
// getBlock
// -----------------------------------------------------------------------------
int Chunk::getBlock(int x, int y, int z) const
{
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        // Out-of-bounds => treat as “-1” or “air”
        return -1;
    }

    // 1D index into m_blocks
    const size_t idx = static_cast<size_t>(x)
        + static_cast<size_t>(SIZE_X) * (static_cast<size_t>(y)
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z));

    return m_blocks[idx];
}

// -----------------------------------------------------------------------------
// setBlock
// -----------------------------------------------------------------------------
void Chunk::setBlock(int x, int y, int z, int voxelID)
{
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        // Out-of-range => ignore
        return;
    }

    const size_t idx = static_cast<size_t>(x)
        + static_cast<size_t>(SIZE_X) * (static_cast<size_t>(y)
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z));

    int oldVal = m_blocks[idx];
    if (oldVal != voxelID)
    {
        // Update the voxel data
        m_blocks[idx] = voxelID;

        // Mark chunk as dirty => needs re-meshing
        m_dirty = true;
        // Typically, once any block changes, we set the state to NORMAL
        // If a terrain generator or post-process finds it is uniform, it will override.
        m_state = ChunkState::NORMAL;
    }
}

// -----------------------------------------------------------------------------
// getBoundingBox (for frustum culling, etc.)
// -----------------------------------------------------------------------------
void Chunk::getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const
{
    float baseX = static_cast<float>(m_worldX * SIZE_X);
    float baseY = static_cast<float>(m_worldY * SIZE_Y);
    float baseZ = static_cast<float>(m_worldZ * SIZE_Z);

    outMin = glm::vec3(baseX, baseY, baseZ);
    outMax = outMin + glm::vec3(SIZE_X, SIZE_Y, SIZE_Z);
}

// -----------------------------------------------------------------------------
// getVoxelUsage
// -----------------------------------------------------------------------------
std::pair<size_t, size_t> Chunk::getVoxelUsage() const
{
    size_t activeCount = 0;
    size_t emptyCount = 0;

    for (int voxelID : m_blocks)
    {
        if (voxelID == 0)
            ++emptyCount;
        else
            ++activeCount;
    }
    return { activeCount, emptyCount };
}

