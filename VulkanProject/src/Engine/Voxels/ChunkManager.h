#pragma once

#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include "Chunk.h"

/* ────────────────────────────────────────────────────────────────────────── */
/* Chunk‑space coordinate key                                                */
/* ────────────────────────────────────────────────────────────────────────── */
struct ChunkCoord
{
    int x, y, z;
    bool operator==(const ChunkCoord& o) const
    {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct ChunkCoordHash
{
    size_t operator()(const ChunkCoord& c) const
    {
        const size_t p = 31;
        size_t h = 1;
        h = h * p + std::hash<int>()(c.x);
        h = h * p + std::hash<int>()(c.y);
        h = h * p + std::hash<int>()(c.z);
        return h;
    }
};

/* ────────────────────────────────────────────────────────────────────────── */
/* ChunkManager – thread‑safe, shared‑ptr ownership                          */
/* ────────────────────────────────────────────────────────────────────────── */
class ChunkManager
{
public:
    ChunkManager();
    ~ChunkManager();

    /* existence check ----------------------------------------------------- */
    bool hasChunk(int cx, int cy, int cz) const;

    /* get / create return shared_ptr so lifetime extends across threads --- */
    std::shared_ptr<Chunk> getChunk(int cx, int cy, int cz) const;
    std::shared_ptr<Chunk> createChunk(int cx, int cy, int cz);

    /* erase (chunk will actually free when last shared_ptr drops) --------- */
    void removeChunk(int cx, int cy, int cz);

    /* read‑only view of map (callers still need to lock if they iterate) -- */
    const std::unordered_map<ChunkCoord,
        std::shared_ptr<Chunk>, ChunkCoordHash>& getAllChunks() const
    {
        return m_chunks;
    }

    /* active / empty voxel usage across all chunks ------------------------ */
    std::pair<size_t, size_t> getTotalVoxelUsage() const;

private:
    mutable std::shared_mutex m_mutex;   // RW lock for m_chunks

    std::unordered_map<ChunkCoord,
        std::shared_ptr<Chunk>, ChunkCoordHash> m_chunks;
};
