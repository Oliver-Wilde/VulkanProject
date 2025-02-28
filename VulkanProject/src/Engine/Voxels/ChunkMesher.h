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
 * It supports naive or "greedy" meshing, plus a new method to mesh from a custom array.
 */
class ChunkMesher
{
public:
    /**
     * Generates a naive mesh of the given chunk by checking each visible face
     * in chunk space, also checking neighbor chunks if needed.
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
     * Greedy meshing approach that merges faces where possible, for fewer triangles.
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
     * A simpler, naive approach used for testing. Ignores adjacency with neighbor chunks.
     */
    void generateMeshNaiveTest(
        const Chunk& chunk,
        std::vector<Vertex>& outVerts,
        std::vector<uint32_t>& outInds,
        int offsetX, int offsetY, int offsetZ
    );

    /**
     * Checks if the chunk is dirty and, if so, generates a mesh (either naive or greedy).
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
    // NEW: For building an LOD mesh from a *downsampled* voxel array.
    // -------------------------------------------------------------------------
    /**
     * Builds a mesh (naive or greedy) from an in-memory voxel array that
     * doesn't necessarily match the chunk's full resolution. This allows
     * generating LOD1, LOD2, etc. from a smaller array.
     *
     * @param voxelArray  A downsampled array of size dsX * dsY * dsZ.
     * @param dsX, dsY, dsZ  Dimensions of the voxelArray.
     * @param worldOffsetX,Y,Z  The world-space offset for these voxels.
     * @param outVertices / outIndices  Where the mesh data is appended.
     * @param useGreedy   If true, merges faces with a greedy approach.
     * @note If you want to consider adjacency with neighbor chunks at LOD,
     *       you'll need more advanced logic. Usually we skip neighbor adjacency at coarser LODs.
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
     */
    static bool isSolidID(int voxelID);

    /**
     * Checks if (x,y,z) is solid, considering neighbor chunks. (Used by naive/greedy mesh.)
     */
    static bool isSolidGlobal(
        const Chunk& currentChunk,
        int cx, int cy, int cz,
        int x, int y, int z,
        const ChunkManager& manager
    );

    // -------------------------------------------------------------------------
    // Internal buildQuad... methods for the greedy approach
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
