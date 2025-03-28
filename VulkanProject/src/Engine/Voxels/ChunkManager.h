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

    bool operator==(const ChunkCoord& other) const
    {
        return (x == other.x && y == other.y && z == other.z);
    }
};

/**
 * Hash struct for unordered_map keying by ChunkCoord.
 */
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
    virtual Chunk* getChunk(int cx, int cy, int cz) const;
    Chunk* createChunk(int cx, int cy, int cz);
    void removeChunk(int cx, int cy, int cz);

    /**
     * Provides read-only access to all chunks in this manager.
     * Inline here in the header to avoid redefinition in .cpp.
     */
    const std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash>& getAllChunks() const
    {
        return m_chunks;
    }

    /**
     * Sums up active and empty voxel counts across all chunks.
     */
    std::pair<size_t, size_t> getTotalVoxelUsage() const;

private:
    // Notice the third template argument is ChunkCoordHash
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> m_chunks;
};
