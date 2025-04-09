#include "Chunk.h"
#include <stdexcept>   // for runtime_error
#include <cstddef>     // for size_t
#include <utility>     // for std::pair
#include <algorithm>   // for std::fill
#include <glm/vec3.hpp>

// -----------------------------------------------------------------------------
// STATIC: track total CPU usage across all chunks
// -----------------------------------------------------------------------------
std::atomic<size_t> Chunk::s_totalCPUBytes{ 0 };

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
Chunk::Chunk(int worldX, int worldY, int worldZ)
    : m_worldX(worldX)
    , m_worldY(worldY)
    , m_worldZ(worldZ)
    , m_state(ChunkState::NORMAL) // Start in NORMAL so we allocate the array
{
    // Allocate the voxel array at full size, initially all 0 (air)
    size_t totalVoxels = static_cast<size_t>(SIZE_X) *
        static_cast<size_t>(SIZE_Y) *
        static_cast<size_t>(SIZE_Z);

    m_blocks.resize(totalVoxels, 0);
    s_totalCPUBytes.fetch_add(totalVoxels * sizeof(uint8_t), std::memory_order_relaxed);

    // LOD init
    for (int i = 0; i < MAX_LOD_LEVELS; i++)
    {
        m_lodGenerated[i] = false;
        m_lodGeomError[i] = 0.0f;
    }
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
Chunk::~Chunk()
{
    // If state == NORMAL, we need to subtract from s_totalCPUBytes
    if (m_state == ChunkState::NORMAL && !m_blocks.empty())
    {
        size_t totalVoxels = m_blocks.size();
        s_totalCPUBytes.fetch_sub(totalVoxels * sizeof(uint8_t), std::memory_order_relaxed);
    }
}

// -----------------------------------------------------------------------------
// makeUniform => free m_blocks, set m_uniformBlockID
// -----------------------------------------------------------------------------
void Chunk::makeUniform(uint8_t uniformID)
{
    // Subtract old array usage from s_totalCPUBytes (if we had allocated)
    size_t oldSize = m_blocks.size();
    if (!m_blocks.empty())
    {
        s_totalCPUBytes.fetch_sub(oldSize * sizeof(uint8_t), std::memory_order_relaxed);
    }

    // Clear the vector
    m_blocks.clear();
    m_blocks.shrink_to_fit();

    // Set our uniform block ID
    m_uniformBlockID = uniformID;
}

// -----------------------------------------------------------------------------
// makeNormal => allocate m_blocks if empty, fill with oldUniformID
// -----------------------------------------------------------------------------
void Chunk::makeNormal(uint8_t oldUniformID)
{
    if (!m_blocks.empty())
    {
        // Already allocated => do nothing
        return;
    }

    // We must allocate the voxel array at full size
    size_t totalVoxels = static_cast<size_t>(SIZE_X) *
        static_cast<size_t>(SIZE_Y) *
        static_cast<size_t>(SIZE_Z);

    m_blocks.resize(totalVoxels, oldUniformID);

    // Add to global CPU usage
    s_totalCPUBytes.fetch_add(totalVoxels * sizeof(uint8_t), std::memory_order_relaxed);
}

// -----------------------------------------------------------------------------
// IBlockProvider::getBlock override
// -----------------------------------------------------------------------------
int Chunk::getBlock(int x, int y, int z) const
{
    // If out-of-range => treat as air
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        return -1; // The mesher will interpret -1 as air
    }

    switch (m_state)
    {
    case ChunkState::EMPTY:
        // All blocks = 0 => air
        return 0;

    case ChunkState::SOLID:
        // Return uniform block ID (nonzero if truly solid, or 0 if it’s all air)
        return static_cast<int>(m_uniformBlockID);

    case ChunkState::NORMAL:
    default:
    {
        // Normal => index into m_blocks
        size_t idx = static_cast<size_t>(x)
            + static_cast<size_t>(SIZE_X) *
            (static_cast<size_t>(y)
                + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z));

        return static_cast<int>(m_blocks[idx]);
    }
    }
}

// -----------------------------------------------------------------------------
// setBlock => store block ID at (x,y,z), possibly forcing a state transition
// -----------------------------------------------------------------------------
void Chunk::setBlock(int x, int y, int z, int voxelID)
{
    if (x < 0 || x >= SIZE_X ||
        y < 0 || y >= SIZE_Y ||
        z < 0 || z >= SIZE_Z)
    {
        // out-of-range => ignore
        return;
    }

    // If chunk is EMPTY and we set a block != 0 => must become NORMAL
    if (m_state == ChunkState::EMPTY)
    {
        if (voxelID != 0)
        {
            makeNormal(/*oldUniformID=*/0);
            m_state = ChunkState::NORMAL;
        }
        else
        {
            // still all air => no change
            return;
        }
    }
    // If chunk is SOLID and we set a block != m_uniformBlockID => must become NORMAL
    else if (m_state == ChunkState::SOLID)
    {
        if (voxelID != m_uniformBlockID)
        {
            makeNormal(/*oldUniformID=*/m_uniformBlockID);
            m_state = ChunkState::NORMAL;
        }
        else
        {
            // same uniform block => no change
            return;
        }
    }
    // else if NORMAL => we already have the array

    // Now we definitely have m_blocks allocated if we’re NORMAL
    size_t idx = static_cast<size_t>(x)
        + static_cast<size_t>(SIZE_X) *
        (static_cast<size_t>(y)
            + static_cast<size_t>(SIZE_Y) * static_cast<size_t>(z));

    int oldVal = static_cast<int>(m_blocks[idx]);
    if (oldVal != voxelID)
    {
        // Actually update the voxel
        m_blocks[idx] = static_cast<uint8_t>(voxelID);
        m_dirty = true;

        // Reset LOD states
        for (int lod = 0; lod < MAX_LOD_LEVELS; lod++)
        {
            m_lodGenerated[lod] = false;
            m_lodGeomError[lod] = 0.0f;
        }
    }
}

// -----------------------------------------------------------------------------
// getBoundingBox => min/max in world space
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
// getVoxelUsage => counts how many non-zero vs zero
// -----------------------------------------------------------------------------
std::pair<size_t, size_t> Chunk::getVoxelUsage() const
{
    // If EMPTY => all air
    if (m_state == ChunkState::EMPTY)
    {
        size_t total = static_cast<size_t>(SIZE_X) * SIZE_Y * SIZE_Z;
        return { 0, total };
    }

    // If SOLID => either all are the same non-zero, or all 0
    if (m_state == ChunkState::SOLID)
    {
        if (m_uniformBlockID == 0)
        {
            // all air
            size_t total = static_cast<size_t>(SIZE_X) * SIZE_Y * SIZE_Z;
            return { 0, total };
        }
        else
        {
            // all active
            size_t total = static_cast<size_t>(SIZE_X) * SIZE_Y * SIZE_Z;
            return { total, 0 };
        }
    }

    // else NORMAL => we must examine m_blocks
    size_t activeCount = 0;
    size_t emptyCount = 0;

    for (auto v : m_blocks)
    {
        if (v == 0) ++emptyCount;
        else        ++activeCount;
    }

    return { activeCount, emptyCount };
}
