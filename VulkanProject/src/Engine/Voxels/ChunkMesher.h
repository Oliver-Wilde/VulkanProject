#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <vector>
#include "Chunk.h"
#include "ChunkManager.h"

// -----------------------------------------------------------------------------
// Struct Definition
// -----------------------------------------------------------------------------
/**
 * Represents a single mesh vertex with position and color (or any other attribute).
 */
struct Vertex
{
    float px, py, pz; ///< Position (x, y, z)
    float cx, cy, cz; ///< Color (r, g, b) or other attribute

    Vertex(float px_, float py_, float pz_,
        float cx_, float cy_, float cz_)
        : px(px_), py(py_), pz(pz_),
        cx(cx_), cy(cy_), cz(cz_)
    {
    }
};

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class ChunkMesher
{
public:
    // -----------------------------------------------------------------------------
    // Public Methods
    // -----------------------------------------------------------------------------

    /**
     * Generates a naive mesh of the given chunk by checking each visible face.
     *
     * @param chunk         Reference to the chunk from which to generate mesh data.
     * @param cx, cy, cz    The chunk coordinates (in chunk-space).
     * @param outVertices   Output vector of vertices.
     * @param outIndices    Output vector of indices.
     * @param offsetX, offsetY, offsetZ  Offset to apply to all positions (usually world position).
     * @param manager       Reference to the ChunkManager for neighbor checks.
     */
    void generateMeshNaive(
        const Chunk& chunk,
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager
    );

    /**
     * Generates a "greedy" mesh of the given chunk by merging faces where possible.
     *
     * @param chunk         Reference to the chunk from which to generate mesh data.
     * @param cx, cy, cz    The chunk coordinates (in chunk-space).
     * @param outVertices   Output vector of vertices.
     * @param outIndices    Output vector of indices.
     * @param offsetX, offsetY, offsetZ  Offset to apply to all positions (usually world position).
     * @param manager       Reference to the ChunkManager for neighbor checks.
     */
    void generateMeshGreedy(
        const Chunk& chunk,
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager
    );

    /**
     * A simple test function demonstrating naive mesh creation without neighbor checks.
     *
     * @param chunk         Reference to the chunk.
     * @param outVerts      Output vector of vertices.
     * @param outInds       Output vector of indices.
     * @param offsetX, offsetY, offsetZ  Offsets for positioning in world space.
     */
    void generateMeshNaiveTest(
        const Chunk& chunk,
        std::vector<Vertex>& outVerts,
        std::vector<uint32_t>& outInds,
        int offsetX, int offsetY, int offsetZ
    );

    /**
     * Checks if the chunk is dirty and, if so, generates a mesh using either naive or greedy meshing.
     *
     * @param chunk         Reference to the chunk (non-const because we may clear the dirty flag).
     * @param cx, cy, cz    The chunk coordinates (in chunk-space).
     * @param outVertices   Output vector of vertices.
     * @param outIndices    Output vector of indices.
     * @param offsetX, offsetY, offsetZ  Offsets for positioning in world space.
     * @param manager       Reference to the ChunkManager for neighbor checks.
     * @param useGreedy     If true, uses greedy meshing; otherwise, uses naive meshing.
     * @return true if the mesh was generated (chunk was dirty), false otherwise.
     */
    bool generateChunkMeshIfDirty(
        Chunk& chunk,
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager,
        bool useGreedy = true
    );

private:
    // -----------------------------------------------------------------------------
    // Private Helper Methods
    // -----------------------------------------------------------------------------

    /**
     * Checks if a given voxelID represents a solid block.
     *
     * @param voxelID The ID of the voxel to check.
     * @return true if solid, false if not.
     */
    static bool isSolidID(int voxelID);

    /**
     * Checks if a voxel is solid in the global sense, taking into account neighboring chunks.
     *
     * @param currentChunk  Reference to the current chunk.
     * @param cx, cy, cz    The chunk coordinates (in chunk-space) of the current chunk.
     * @param x, y, z       Local coordinates within the chunk.
     * @param manager       Reference to the ChunkManager for neighbor checks.
     * @return true if solid, false otherwise.
     */
    static bool isSolidGlobal(
        const Chunk& currentChunk,
        int cx, int cy, int cz,
        int x, int y, int z,
        const ChunkManager& manager
    );

    // -----------------------------------------------------------------------------
    // Build Quad Methods (used in greedy meshing)
    // -----------------------------------------------------------------------------
    void buildQuadPosZ(
        int startX, int startY, int width, int height, int z,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices
    );

    void buildQuadNegZ(
        int startX, int startY, int width, int height, int z,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices
    );

    void buildQuadPosX(
        int startY, int startZ, int height, int depth, int x,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices
    );

    void buildQuadNegX(
        int startY, int startZ, int height, int depth, int x,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices
    );

    void buildQuadPosY(
        int startX, int startZ, int width, int depth, int y,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices
    );

    void buildQuadNegY(
        int startX, int startZ, int width, int depth, int y,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices
    );
};
