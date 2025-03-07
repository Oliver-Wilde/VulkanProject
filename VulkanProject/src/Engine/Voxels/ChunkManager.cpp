#include "ChunkManager.h"
#include <stdexcept>
#include <Engine/Utils/Logger.h>

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------
ChunkManager::ChunkManager()
{
}

ChunkManager::~ChunkManager()
{
    // unique_ptr automatically cleans up all Chunks in m_chunks
}

// -----------------------------------------------------------------------------
// Public Methods
// -----------------------------------------------------------------------------
bool ChunkManager::hasChunk(int cx, int cy, int cz) const
{
    ChunkCoord coord{ cx, cy, cz };
    auto it = m_chunks.find(coord);
    return (it != m_chunks.end());
}

Chunk* ChunkManager::getChunk(int cx, int cy, int cz) const
{
    ChunkCoord coord{ cx, cy, cz };
    auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return nullptr;
    }
    return it->second.get();
}

Chunk* ChunkManager::createChunk(int cx, int cy, int cz)
{
    ChunkCoord coord{ cx, cy, cz };
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end()) {
        return it->second.get();
    }

    std::unique_ptr<Chunk> newChunk = std::make_unique<Chunk>(cx, cy, cz);
    Chunk* chunkPtr = newChunk.get();
    m_chunks.emplace(coord, std::move(newChunk));

    Logger::Info("Creating chunk at ("
        + std::to_string(cx) + ", "
        + std::to_string(cy) + ", "
        + std::to_string(cz) + ")");

    return chunkPtr;
}

// -----------------------------------------------------------------------------
// removeChunk: remove a chunk immediately
// -----------------------------------------------------------------------------
void ChunkManager::removeChunk(int cx, int cy, int cz)
{
    ChunkCoord coord{ cx, cy, cz };
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end())
    {
        m_chunks.erase(it);

        Logger::Info("Removing chunk at ("
            + std::to_string(cx) + ", "
            + std::to_string(cy) + ", "
            + std::to_string(cz) + ")");
    }
}

/* -----------------------------------------------------------------------------
   [CHANGED] (Optional) You can add a "batch removal" approach if you want to
   remove multiple chunks gradually over several frames.

   If you decide to implement it, you'd:
   1) Push chunk coords to an internal queue or vector (m_pendingRemovals).
   2) Each frame, call removeChunksBatch(N) to remove up to N queued chunks.
   3) That way, you never remove too many in one frame.
   -----------------------------------------------------------------------------

// Example: queue a chunk for removal later
void ChunkManager::scheduleRemoveChunk(int cx, int cy, int cz)
{
    ChunkCoord coord{ cx, cy, cz };
    m_pendingRemovals.push_back(coord);
}

// Example: remove up to 'maxCount' scheduled chunks in this frame
void ChunkManager::removeChunksBatch(int maxCount)
{
    int removedThisFrame = 0;
    while (!m_pendingRemovals.empty() && removedThisFrame < maxCount)
    {
        ChunkCoord coord = m_pendingRemovals.back();
        m_pendingRemovals.pop_back();

        removeChunk(coord.x, coord.y, coord.z);
        removedThisFrame++;
    }
}
*/

// -----------------------------------------------------------------------------
// getTotalVoxelUsage
// -----------------------------------------------------------------------------
std::pair<size_t, size_t> ChunkManager::getTotalVoxelUsage() const
{
    size_t totalActive = 0;
    size_t totalEmpty = 0;

    for (const auto& kv : m_chunks) {
        std::pair<size_t, size_t> usage = kv.second->getVoxelUsage();
        totalActive += usage.first;
        totalEmpty += usage.second;
    }
    return { totalActive, totalEmpty };
}
