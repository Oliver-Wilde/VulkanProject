#pragma once

#include <vector>
#include <cstdint>

// Forward declarations so we don't need to include everything in this header:
class Chunk;
class ChunkManager;
struct NVertex;

namespace ChunkMesherNaive
{

    struct NVertex
    {
        float px, py, pz; ///< Position (x, y, z)
        float cx, cy, cz; ///< Color (r, g, b) or other attribute

        NVertex(float px_, float py_, float pz_,
            float cx_, float cy_, float cz_)
            : px(px_), py(py_), pz(pz_),
            cx(cx_), cy(cy_), cz(cz_)
        {
        }
    };
    /**
     * \brief Checks if a voxelID is solid (non-air).
     */
    bool isSolidID(int voxelID);

    /**
     * \brief Checks if the voxel at local coords (x, y, z) is solid, possibly in a neighbor chunk.
     * \param currentChunk The chunk we’re in.
     * \param cx, cy, cz   Chunk-space coordinates of currentChunk.
     * \param x, y, z      Local voxel coordinates within that chunk (or neighbor).
     * \param manager      Reference to ChunkManager, for retrieving neighbor chunks.
     * \return true if solid, false otherwise
     */
    bool isSolidGlobal(
        const Chunk& currentChunk,
        int cx, int cy, int cz,
        int x, int y, int z,
        const ChunkManager& manager
    );

    /**
     * \brief Generate mesh data using naive adjacency checks. Every face is tested individually
     *        to see if it’s exposed.
     *
     * \param chunk       The chunk whose voxels we’re meshing.
     * \param cx, cy, cz  The chunk's coordinate in chunk-space.
     * \param[out] outVertices  Vector to receive generated vertices.
     * \param[out] outIndices   Vector to receive generated indices.
     * \param offsetX, offsetY, offsetZ  Offsets added to each vertex (e.g., world-space).
     * \param manager     Reference to the ChunkManager (for neighbor lookups).
     */
    void generateMeshNaive(
        const Chunk& chunk,
        int cx,
        int cy,
        int cz,
        std::vector<NVertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX,
        int offsetY,
        int offsetZ,
        const ChunkManager& manager
    );

    /**
     * \brief A "test" variant that places all six faces of every voxel, ignoring adjacency.
     * \param chunk    The chunk whose voxels we’re meshing.
     * \param[out] outVerts  Vector to receive generated vertices.
     * \param[out] outInds   Vector to receive generated indices.
     * \param offsetX, offsetY, offsetZ  Offsets added to each vertex (e.g., world-space).
     */
    void generateMeshNaiveTest(
        const Chunk& chunk,
        std::vector<NVertex>& outVerts,
        std::vector<uint32_t>& outInds,
        int offsetX,
        int offsetY,
        int offsetZ
    );
} // namespace ChunkMesherNaive
