#pragma once

#include <vector>
#include "Chunk.h"
#include "ChunkManager.h"

struct Vertex
{
    float px, py, pz;
    float cx, cy, cz;

    Vertex(float px_, float py_, float pz_,
        float cx_, float cy_, float cz_)
        : px(px_), py(py_), pz(pz_),
        cx(cx_), cy(cy_), cz(cz_)
    {
    }
};

class ChunkMesher
{
public:
    // Existing naive + greedy
    void generateMeshNaive(
        const Chunk& chunk,
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager
    );

    void generateMeshGreedy(
        const Chunk& chunk,
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager
    );

    void generateMeshNaiveTest(const Chunk& chunk, std::vector<Vertex>& outVerts, std::vector<uint32_t>& outInds, int offsetX, int offsetY, int offsetZ);

    // NEW: A helper that checks if the chunk is dirty, and if so, runs a meshing function
    bool generateChunkMeshIfDirty(
        Chunk& chunk,                     // chunk is not const because we might clear dirty
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager,
        bool useGreedy = true            // choose which meshing approach
    );

private:
    // The usual adjacency checks
    static bool isSolidID(int voxelID);
    static bool isSolidGlobal(const Chunk& currentChunk,
        int cx, int cy, int cz,
        int x, int y, int z,
        const ChunkManager& manager);

    // Helpers for building quads in each direction
    void buildQuadPosZ(int startX, int startY, int width, int height, int z,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);

    // We'll define the other 5 directions below
    void buildQuadNegZ(int startX, int startY, int width, int height, int z,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);

    void buildQuadPosX(int startY, int startZ, int height, int depth, int x,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);

    void buildQuadNegX(int startY, int startZ, int height, int depth, int x,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);

    void buildQuadPosY(int startX, int startZ, int width, int depth, int y,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);

    void buildQuadNegY(int startX, int startZ, int width, int depth, int y,
        int offsetX, int offsetY, int offsetZ, int blockID,
        std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);

};