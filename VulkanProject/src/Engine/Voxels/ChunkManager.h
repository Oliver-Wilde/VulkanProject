#pragma once

#include <unordered_map>
#include <memory>
#include "Chunk.h"

/**
 * Represents a coordinate in chunk-space.
 */
struct ChunkCoord
{
    int x;
    int y;
    int z;

    ChunkCoord(int x_, int y_, int z_)
        : x(x_), y(y_), z(z_) {}

    bool operator==(const ChunkCoord& other) const
    {
        return (x == other.x && y == other.y && z == other.z);
    }
};

// Hash functor for ChunkCoord
struct ChunkCoordHash
{
    size_t operator()(const ChunkCoord& coord) const
    {
        // A simple way to hash 3 ints
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

    /**
     * Read-only access to map of (ChunkCoord -> unique_ptr<Chunk>).
     */
    const std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash>& getAllChunks() const
    {
        return m_chunks;
    }

    // For debug usage stats
    std::pair<size_t, size_t> getTotalVoxelUsage() const;

private:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> m_chunks;
};