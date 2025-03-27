#include "Chunk.h"
#include <stdexcept>       // For runtime_error if needed
#include <cstddef>         // For size_t
#include <glm/vec3.hpp>    // For glm::vec3
#include <utility>         // For std::pair

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------
Chunk::Chunk(int worldX, int worldY, int worldZ)
    : m_worldX(worldX)
    , m_worldY(worldY)
    , m_worldZ(worldZ)
    , m_dirty(true)
    , m_isUploading(false)
    , m_state(ChunkState::NORMAL) // Default
{
    // Allocate the voxel array
    size_t total = size_t(SIZE_X) * size_t(SIZE_Y) * size_t(SIZE_Z);
    m_blocks.resize(total, 0); // 0 => "Air"
}

Chunk::~Chunk()
{
    // Typically we don't destroy GPU buffers here,
    // since VoxelWorld or ResourceManager handle it.
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

    size_t idx = size_t(x)
        + size_t(SIZE_X) * (size_t(y)
            + size_t(SIZE_Y) * size_t(z));
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

    size_t idx = size_t(x)
        + size_t(SIZE_X) * (size_t(y)
            + size_t(SIZE_Y) * size_t(z));
    int oldVal = m_blocks[idx];

    if (oldVal != voxelID)
    {
        // Update the voxel ID
        m_blocks[idx] = voxelID;
        // Mark chunk dirty => re-mesh
        m_dirty = true;

        // We also reset the state to NORMAL here, but
        // realistically we'll do a final pass in TerrainGenerator
        // to set it to EMPTY or SOLID if needed.
        m_state = ChunkState::NORMAL;
    }
}

// -----------------------------------------------------------------------------
// getBoundingBox
// -----------------------------------------------------------------------------
void Chunk::getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const
{
    // Convert chunk coords to world space
    float baseX = float(m_worldX * SIZE_X);
    float baseY = float(m_worldY * SIZE_Y);
    float baseZ = float(m_worldZ * SIZE_Z);

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
