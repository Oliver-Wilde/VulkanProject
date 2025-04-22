#include "ChunkManager.h"
#include <Engine/Utils/Logger.h>

#include <shared_mutex>

ChunkManager::ChunkManager() = default;
ChunkManager::~ChunkManager() = default;

/* ────────────────────────────────────────────────────────────────────────── */
/* presence check (reader lock)                                              */
/* ────────────────────────────────────────────────────────────────────────── */
bool ChunkManager::hasChunk(int cx, int cy, int cz) const
{
    std::shared_lock<std::shared_mutex> rd(m_mutex);
    return m_chunks.find({ cx, cy, cz }) != m_chunks.end();
}

/* ────────────────────────────────────────────────────────────────────────── */
/* getChunk – returns shared_ptr so lifetime extends across threads          */
/* ────────────────────────────────────────────────────────────────────────── */
std::shared_ptr<Chunk> ChunkManager::getChunk(int cx, int cy, int cz) const
{
    std::shared_lock<std::shared_mutex> rd(m_mutex);
    auto it = m_chunks.find({ cx, cy, cz });
    return (it != m_chunks.end()) ? it->second : nullptr;
}

/* ────────────────────────────────────────────────────────────────────────── */
/* createChunk (writer lock)                                                 */
/* ────────────────────────────────────────────────────────────────────────── */
std::shared_ptr<Chunk> ChunkManager::createChunk(int cx, int cy, int cz)
{
    std::unique_lock<std::shared_mutex> wr(m_mutex);

    ChunkCoord key{ cx, cy, cz };
    auto it = m_chunks.find(key);
    if (it != m_chunks.end()) return it->second;

    auto chunk = std::make_shared<Chunk>(cx, cy, cz);
    m_chunks.emplace(key, chunk);

    Logger::Info("Creating chunk at (" +
        std::to_string(cx) + ", " +
        std::to_string(cy) + ", " +
        std::to_string(cz) + ")");

    return chunk;
}

/* ────────────────────────────────────────────────────────────────────────── */
/* removeChunk (writer lock) – shared_ptr ensures safe deferred destruction  */
/* ────────────────────────────────────────────────────────────────────────── */
void ChunkManager::removeChunk(int cx, int cy, int cz)
{
    std::unique_lock<std::shared_mutex> wr(m_mutex);

    ChunkCoord key{ cx, cy, cz };
    auto it = m_chunks.find(key);
    if (it != m_chunks.end())
    {
        Logger::Info("Removing chunk at (" +
            std::to_string(cx) + ", " +
            std::to_string(cy) + ", " +
            std::to_string(cz) + ")");
        m_chunks.erase(it);      // actual delete occurs when last ref drops
    }
}

/* ────────────────────────────────────────────────────────────────────────── */
/* aggregate voxel usage (reader lock)                                       */
/* ────────────────────────────────────────────────────────────────────────── */
std::pair<size_t, size_t> ChunkManager::getTotalVoxelUsage() const
{
    std::shared_lock<std::shared_mutex> rd(m_mutex);

    size_t active = 0, empty = 0;
    for (const auto& kv : m_chunks)
    {
        auto usage = kv.second->getVoxelUsage();
        active += usage.first;
        empty += usage.second;
    }
    return { active, empty };
}
