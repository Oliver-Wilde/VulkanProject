#pragma once

#include <vector>
#include "Chunk.h"
#include "ChunkManager.h"

/**
 * Represents a single mesh vertex with position + color (or other).
 */
struct Vertex
{
    float px, py, pz; ///< position
    float cx, cy, cz; ///< color

    Vertex(float px_, float py_, float pz_,
        float cx_, float cy_, float cz_)
        : px(px_), py(py_), pz(pz_),
        cx(cx_), cy(cy_), cz(cz_)
    {}
};

/**
 * The ChunkMesher class can build:
 *  - Normal LOD geometry for each chunk
 *  - Optional seam geometry bridging chunk boundaries
 */
class ChunkMesher
{
public:
    /**
     * Generates a "greedy" mesh for LOD0 (or the chunk’s data).
     * Checks neighbor blocks to skip hidden faces.
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
     * Builds a mesh from a downsampled array (for LOD1, LOD2, etc.).
     * Optionally merges internal faces if useGreedy==true.
     * Does NOT do cross-chunk boundaries for LODs.
     */
    void generateMeshFromArray(
        const std::vector<int>& voxelArray,
        int dsX, int dsY, int dsZ,
        int worldOffsetX, int worldOffsetY, int worldOffsetZ,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        bool useGreedy = false
    );

    /**
     * (Legacy) If LOD0 is dirty, build the chunk. This remains basically
     * the same but references the new generateMeshGreedy method.
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
    // SEAM / STITCH Methods (placeholders)
    // -------------------------------------------------------------------------
    /**
     * Build a "stitch" mesh bridging chunk A’s face with chunk B’s face,
     * if they differ by exactly 1 in LOD.
     *
     * This is a placeholder demonstration. The full logic depends on how
     * you want to morph or subdivide the boundary.
     */
    void buildLODBoundaryStitch(
        const Chunk& chunkA,
        int lodA,
        const Chunk& chunkB,
        int lodB,
        // Possibly info about the shared boundary or direction
        // ...
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices
    );

private:

    /**
     * Helper that returns block ID from current or neighbor chunk,
     * or 0 if neighbor is missing.  (Used in LOD0 to skip faces.)
     */
    static int getBlockIDGlobal(
        const Chunk& currentChunk,
        int cx, int cy, int cz,
        int x, int y, int z,
        const ChunkManager& manager
    );

    // -------------------------------------------------------------------------
    // Internal buildQuad... methods
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
