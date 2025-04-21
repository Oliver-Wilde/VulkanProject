#include "Chunk.h"

#include <stdexcept>
#include <cstddef>
#include <utility>
#include <algorithm>
#include <limits>
#include <cstdint>
#include <glm/vec3.hpp>
#include <atomic>

/*─────────────────────────────────────────────────────────────────────────────
  Helper: 64‑bit FNV‑1a hash
 ────────────────────────────────────────────────────────────────────────────*/
static uint64_t fnv1a64(const void* data, size_t len)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t h = 14695981039346656037ull;
    while (len--) { h ^= *p++; h *= 1099511628211ull; }
    return h;
}

/*─────────────────────────────────────────────────────────────────────────────*/
std::atomic<size_t> Chunk::s_totalCPUBytes{ 0 };

/*=============================================================================
  Constructor / Destructor
=============================================================================*/
Chunk::Chunk(int worldX, int worldY, int worldZ)
    : m_worldX(worldX)
    , m_worldY(worldY)
    , m_worldZ(worldZ)
    , m_state(ChunkState::NORMAL)
{
    size_t voxels = size_t(SIZE_X) * SIZE_Y * SIZE_Z;
    m_blocks.resize(voxels, 0);
    s_totalCPUBytes.fetch_add(voxels * sizeof(uint8_t), std::memory_order_relaxed);

    m_hasValidBounds = false;
    m_hashDirty = true;
}

Chunk::~Chunk()
{
    if (m_state == ChunkState::NORMAL && !m_blocks.empty())
    {
        s_totalCPUBytes.fetch_sub(m_blocks.size() * sizeof(uint8_t),
            std::memory_order_relaxed);
    }
}

/*=============================================================================
  State helpers
=============================================================================*/
void Chunk::makeUniform(uint8_t uniformID)
{
    if (!m_blocks.empty())
        s_totalCPUBytes.fetch_sub(m_blocks.size() * sizeof(uint8_t),
            std::memory_order_relaxed);

    m_blocks.clear();
    m_blocks.shrink_to_fit();
    m_uniformBlockID = uniformID;
    m_hasValidBounds = false;
    markHashDirty();
}

void Chunk::makeNormal(uint8_t oldUniformID)
{
    if (!m_blocks.empty()) return;

    size_t voxels = size_t(SIZE_X) * SIZE_Y * SIZE_Z;
    m_blocks.resize(voxels, oldUniformID);
    s_totalCPUBytes.fetch_add(voxels * sizeof(uint8_t), std::memory_order_relaxed);

    m_hasValidBounds = false;
    markHashDirty();
}

/*=============================================================================
  IBlockProvider
=============================================================================*/
int Chunk::getBlock(int x, int y, int z) const
{
    if (x < 0 || x >= SIZE_X || y < 0 || y >= SIZE_Y || z < 0 || z >= SIZE_Z)
        return -1;

    switch (m_state)
    {
    case ChunkState::EMPTY:  return 0;
    case ChunkState::SOLID:  return int(m_uniformBlockID);
    case ChunkState::NORMAL:
    default:
        if (m_blocks.empty()) return 0;
        size_t idx = size_t(x) + size_t(SIZE_X) *
            (size_t(y) + size_t(SIZE_Y) * size_t(z));
        return int(m_blocks[idx]);
    }
}

/*=============================================================================
  setBlock
=============================================================================*/
void Chunk::setBlock(int x, int y, int z, int voxelID)
{
    if (x < 0 || x >= SIZE_X || y < 0 || y >= SIZE_Y || z < 0 || z >= SIZE_Z)
        return;

    if (m_state == ChunkState::EMPTY && voxelID != 0)
    {
        makeNormal(0);
        m_state = ChunkState::NORMAL;
    }
    else if (m_state == ChunkState::SOLID && voxelID != m_uniformBlockID)
    {
        makeNormal(m_uniformBlockID);
        m_state = ChunkState::NORMAL;
    }

    if (m_state != ChunkState::NORMAL) return;

    size_t idx = size_t(x) + size_t(SIZE_X) *
        (size_t(y) + size_t(SIZE_Y) * size_t(z));

    if (m_blocks[idx] == voxelID) return;

    m_blocks[idx] = uint8_t(voxelID);
    m_dirty = true;
    m_hasValidBounds = false;
    markHashDirty();

    for (int i = 0; i < MAX_LOD_LEVELS; ++i)
    {
        m_lodGenerated[i] = false;
        m_lodGeomError[i] = 0.0f;
    }
}

/*=============================================================================
  Bounding‑box helpers
=============================================================================*/
void Chunk::getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const
{
    if (m_state == ChunkState::EMPTY)
    {
        outMin = outMax = glm::vec3(0.0f);
        return;
    }

    if (m_state == ChunkState::SOLID || !m_hasValidBounds)
    {
        float bx = float(m_worldX * SIZE_X);
        float by = float(m_worldY * SIZE_Y);
        float bz = float(m_worldZ * SIZE_Z);
        outMin = glm::vec3(bx, by, bz);
        outMax = outMin + glm::vec3(SIZE_X, SIZE_Y, SIZE_Z);
        return;
    }

    float bx = float(m_worldX * SIZE_X);
    float by = float(m_worldY * SIZE_Y);
    float bz = float(m_worldZ * SIZE_Z);

    outMin = glm::vec3(bx + m_localMinFilled.x,
        by + m_localMinFilled.y,
        bz + m_localMinFilled.z);

    outMax = glm::vec3(bx + m_localMaxFilled.x + 1.0f,
        by + m_localMaxFilled.y + 1.0f,
        bz + m_localMaxFilled.z + 1.0f);
}

/*=============================================================================
  recalcFilledBounds
=============================================================================*/
void Chunk::recalcFilledBounds()
{
    if (m_state == ChunkState::EMPTY)
    {
        m_hasValidBounds = false;
        return;
    }
    if (m_state == ChunkState::SOLID)
    {
        m_hasValidBounds = true;
        m_localMinFilled = glm::ivec3(0);
        m_localMaxFilled = glm::ivec3(SIZE_X - 1, SIZE_Y - 1, SIZE_Z - 1);
        return;
    }

    if (m_blocks.empty()) { m_hasValidBounds = false; return; }

    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int minZ = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();
    int maxZ = std::numeric_limits<int>::min();

    for (int z = 0; z < SIZE_Z; ++z)
        for (int y = 0; y < SIZE_Y; ++y)
            for (int x = 0; x < SIZE_X; ++x)
            {
                size_t idx = size_t(x) + size_t(SIZE_X) *
                    (size_t(y) + size_t(SIZE_Y) * size_t(z));
                if (m_blocks[idx] == 0) continue;

                minX = std::min(minX, x); minY = std::min(minY, y); minZ = std::min(minZ, z);
                maxX = std::max(maxX, x); maxY = std::max(maxY, y); maxZ = std::max(maxZ, z);
            }

    if (maxX < minX)
    {
        m_hasValidBounds = false;
    }
    else
    {
        m_hasValidBounds = true;
        m_localMinFilled = glm::ivec3(minX, minY, minZ);
        m_localMaxFilled = glm::ivec3(maxX, maxY, maxZ);
    }
}

/*=============================================================================
  getVoxelUsage
=============================================================================*/
std::pair<size_t, size_t> Chunk::getVoxelUsage() const
{
    if (m_state == ChunkState::EMPTY)
    {
        size_t total = size_t(SIZE_X) * SIZE_Y * SIZE_Z;
        return { 0, total };
    }

    if (m_state == ChunkState::SOLID)
    {
        size_t total = size_t(SIZE_X) * SIZE_Y * SIZE_Z;
        if (m_uniformBlockID == 0)
            return { 0, total };          // all air
        else
            return { total, 0 };          // all solid
    }

    size_t active = 0, empty = 0;
    for (uint8_t v : m_blocks) (v == 0) ? ++empty : ++active;
    return { active, empty };
}

/*=============================================================================
  Content hash
=============================================================================*/
uint64_t Chunk::getContentHash()
{
    if (!m_hashDirty) return m_cachedHash;

    uint64_t h = 14695981039346656037ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };

    mix(uint64_t(m_worldX));
    mix(uint64_t(m_worldY));
    mix(uint64_t(m_worldZ));
    mix(uint64_t(m_state));
    mix(uint64_t(m_uniformBlockID));

    if (m_state == ChunkState::NORMAL && !m_blocks.empty())
        h ^= fnv1a64(m_blocks.data(), m_blocks.size());

    m_cachedHash = h;
    m_hashDirty = false;
    return h;
}
