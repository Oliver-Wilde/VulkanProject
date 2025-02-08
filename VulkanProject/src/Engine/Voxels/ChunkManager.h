#pragma once

#include <unordered_map>
#include <memory>
#include "Chunk.h"

struct ChunkCoord
{
    int x;
    int y;
    int z;

    bool operator==(const ChunkCoord& other) const
    {
        return (x == other.x && y == other.y && z == other.z);
    }
};

struct ChunkCoordHash
{
    size_t operator()(const ChunkCoord& coord) const
    {
        const size_t prime = 31;
        size_t result = 1;
        result = result * prime + std::hash<int>()(coord.x);
        result = result * prime + std::hash<int>()(coord.y);
        result = result * prime + std::hash<int>()(coord.z);
        return result;
    }
};

class ChunkManager
{
public:
    ChunkManager();
    ~ChunkManager();

    bool hasChunk(int cx, int cy, int cz) const;
    Chunk* getChunk(int cx, int cy, int cz) const;
    Chunk* createChunk(int cx, int cy, int cz);
    void   removeChunk(int cx, int cy, int cz);

    const std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash>& getAllChunks() const
    {
        return m_chunks;
    }

private:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> m_chunks;
};