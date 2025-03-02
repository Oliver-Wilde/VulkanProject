#pragma once

#include <vector>
#include "Chunk.h"
#include "ChunkManager.h"

/**
 * Represents a single mesh vertex with position and color (or any other attribute).
 */
struct Vertex
{
    float px, py, pz; ///< Position (x, y, z)
    float cx, cy, cz; ///< Color (r, g, b)

    Vertex(float px_, float py_, float pz_,
        float cx_, float cy_, float cz_)
        : px(px_), py(py_), pz(pz_),
        cx(cx_), cy(cy_), cz(cz_)
    {}
};

/**
 * The ChunkMesher class can build a mesh (vertices + indices) from a chunk’s voxel data.
 * It supports naive or "greedy" meshing, plus a method to mesh from a custom array.
 *
 * Features:
 *  - Boundary merging: if a neighbor chunk has the same voxel ID, skip that face.
 *  - LOD meshing with local adjacency checks if desired.
 */
class ChunkMesher
{
public:
    /**
     * Generates a naive mesh of the given chunk by checking each visible face
     * in chunk space, comparing block IDs across chunk boundaries.
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
     * Greedy meshing approach that merges faces where possible (fewer triangles),
     * also skipping boundaries shared with neighbor chunks if they have the same voxel ID.
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
     * A simple naive approach used for testing. Ignores adjacency with neighbor chunks.
     * (Doesn't do cross-chunk boundary merges.)
     */
    void generateMeshNaiveTest(
        const Chunk& chunk,
        std::vector<Vertex>& outVerts,
        std::vector<uint32_t>& outInds,
        int offsetX, int offsetY, int offsetZ
    );

    /**
     * Checks if LOD0 is dirty and, if so, generates a mesh (either naive or greedy).
     * This is your existing function, focusing on LOD0 usage.
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

    // -------------------------------------------------------------------------
    // For building an LOD mesh from a downsampled voxel array.
    // -------------------------------------------------------------------------
    /**
     * Builds a mesh (naive or greedy) from an in-memory voxel array that
     * doesn't necessarily match the chunk's full resolution (dsX * dsY * dsZ).
     *
     * @param voxelArray   Downsampled array (dsX * dsY * dsZ) of block IDs.
     * @param dsX, dsY, dsZ  Dimensions of that downsampled array.
     * @param worldOffsetX,Y,Z  World-space offset for these voxels.
     * @param outVertices / outIndices  Output mesh data.
     * @param useGreedy    Whether to merge faces. Typically no neighbor adjacency at LOD.
     */
    void generateMeshFromArray(
        const std::vector<int>& voxelArray,
        int dsX, int dsY, int dsZ,
        int worldOffsetX, int worldOffsetY, int worldOffsetZ,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        bool useGreedy = false
    );

private:
    /**
     * Checks if a voxelID is "solid" by looking up its VoxelType in VoxelTypeRegistry.
     * Still used in some older code or for quick checks (LOD, etc.).
     */
    static bool isSolidID(int voxelID);

    /**
     * OLD method that returns 'true' if (x,y,z) is solid in current or neighbor chunk.
     * This is kept for backward compatibility, but boundary merges now rely on
     * exact block ID comparison (see getBlockIDGlobal below).
     */
    static bool isSolidGlobal(
        const Chunk& currentChunk,
        int cx, int cy, int cz,
        int x, int y, int z,
        const ChunkManager& manager
    );

    /**
     * NEW helper: returns the actual block ID from current chunk or neighbor chunk,
     * or -1 if the neighbor chunk is missing or out-of-bounds. Used to unify boundaries.
     */
    static int getBlockIDGlobal(
        const Chunk& currentChunk,
        int cx, int cy, int cz,
        int x, int y, int z,
        const ChunkManager& manager
    );

    // -------------------------------------------------------------------------
    // Internal buildQuad... methods for the greedy approach.
    // These remain the same, no direct boundary logic here.
    // -------------------------------------------------------------------------
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
