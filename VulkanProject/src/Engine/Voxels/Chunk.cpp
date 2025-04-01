#include "Chunk.h"
#include <stdexcept>  // For runtime_error
#include <cstddef>    // For size_t
#include <glm/vec3.hpp>
#include <utility>    // For std::pair>

// Define the static atomic
std::atomic<size_t> Chunk::s_totalCPUBytes{ 0 };

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
    const size_t total = static_cast<size_t>(SIZE_X) * static_cast<size_t>(SIZE_Y) * static_cast<size_t>(SIZE_Z);
    m_blocks.resize(total, 0); // 0 => "Air"

    // Track CPU usage
    s_totalCPUBytes.fetch_add(total * sizeof(uint8_t), std::memory_order_relaxed);

    // Default all LOD states to "not generated" and error metric to 0.0f
    for (int i = 0; i < MAX_LOD_LEVELS; i++)
    {
        m_lodGenerated[i] = false;
        m_lodGeomError[i] = 0.0f;
    }
}

Chunk::~Chunk()
{
    // Reduce the CPU usage counter
    const size_t total = static_cast<size_t>(SIZE_X) * static_cast<size_t>(SIZE_Y) * static_cast<size_t>(SIZE_Z);
    s_totalCPUBytes.fetch_sub(total * sizeof(uint8_t), std::memory_order_relaxed);

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

    return static_cast<int>(m_blocks[idx]);
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

    const int oldVal = static_cast<int>(m_blocks[idx]);
    if (oldVal != voxelID)
    {
        // Update the voxel data
        m_blocks[idx] = static_cast<uint8_t>(voxelID);

        // Mark chunk as dirty => needs re-meshing
        m_dirty = true;
        // If any block changes, assume chunk is no longer uniform => NORMAL
        m_state = ChunkState::NORMAL;

        // Once we alter a block, the previously generated LODs are invalid
        for (int lod = 0; lod < MAX_LOD_LEVELS; lod++)
        {
            m_lodGenerated[lod] = false;
            m_lodGeomError[lod] = 0.0f;
        }
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

    for (auto voxelID : m_blocks)
    {
        if (voxelID == 0)
            ++emptyCount;
        else
            ++activeCount;
    }
    return { activeCount, emptyCount };
}
