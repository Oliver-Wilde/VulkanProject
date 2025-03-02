#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <unordered_map>
#include <memory>
#include "Chunk.h"

// -----------------------------------------------------------------------------
// Struct Definitions
// -----------------------------------------------------------------------------
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

    // This operator allows '==' to compile:
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

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class ChunkManager
{
public:
    // -----------------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------------
    ChunkManager();
    ~ChunkManager();

    // -----------------------------------------------------------------------------
    // Public Methods
    // -----------------------------------------------------------------------------
    /**
     * Checks whether a chunk exists at the specified chunk coordinates.
     *
     * @param cx Chunk X coordinate.
     * @param cy Chunk Y coordinate.
     * @param cz Chunk Z coordinate.
     * @return True if the chunk exists, false otherwise.
     */
    bool hasChunk(int cx, int cy, int cz) const;

    /**
     * Retrieves a pointer to the chunk at the specified chunk coordinates.
     *
     * @param cx Chunk X coordinate.
     * @param cy Chunk Y coordinate.
     * @param cz Chunk Z coordinate.
     * @return Pointer to the Chunk if found, otherwise nullptr.
     */
    Chunk* getChunk(int cx, int cy, int cz) const;

    /**
     * Creates and returns a new chunk at the specified chunk coordinates if it doesn't exist.
     * If it does exist, returns a pointer to the existing chunk.
     *
     * @param cx Chunk X coordinate.
     * @param cy Chunk Y coordinate.
     * @param cz Chunk Z coordinate.
     * @return Pointer to the newly created or existing chunk.
     */
    Chunk* createChunk(int cx, int cy, int cz);

    /**
     * Removes the chunk at the specified chunk coordinates.
     *
     * @param cx Chunk X coordinate.
     * @param cy Chunk Y coordinate.
     * @param cz Chunk Z coordinate.
     */
    void removeChunk(int cx, int cy, int cz);

    /**
     * Provides read-only access to all chunks managed by this ChunkManager.
     *
     * @return A const reference to the internal map of chunk coordinates to chunks.
     */
    const std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash>& getAllChunks() const
    {
        return m_chunks;
    }

    std::pair<size_t, size_t> getTotalVoxelUsage() const;
private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    /**
     * A map from chunk coordinates (ChunkCoord) to the actual chunk data (unique_ptr<Chunk>).
     */
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>, ChunkCoordHash> m_chunks;
};
