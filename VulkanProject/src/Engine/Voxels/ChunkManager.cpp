
#include "ChunkManager.h"
#include <stdexcept>
#include <Engine/Utils/Logger.h>

ChunkManager::ChunkManager()
{
}

ChunkManager::~ChunkManager()
{
    // unique_ptr automatically cleans up all Chunks in m_chunks
}

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

    Logger::Info("Creating chunk at (" + std::to_string(cx) + ", "
        + std::to_string(cy) + ", "
        + std::to_string(cz) + ")");


    return chunkPtr;
}

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