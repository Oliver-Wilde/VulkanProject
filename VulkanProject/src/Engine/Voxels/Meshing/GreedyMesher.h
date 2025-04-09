#ifndef GREEDY_MESHER_H
#define GREEDY_MESHER_H

#include "IMesher.h"
#include "Engine/Voxels/IBlockProvider.h"

/**
 * A mesher that merges adjacent faces into large quads,
 * drastically reducing polygon count for blocky terrain.
 */
class GreedyMesher : public IMesher
{
public:
    virtual ~GreedyMesher() = default;

    /**
     * generateMesh override from IMesher
     * => Called when we want to mesh an actual Chunk for LOD0 or single-lod usage.
     */
    bool generateMesh(
        Chunk& chunk,
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager
    ) const override;

    /**
     * generateMesh for any IBlockProvider (e.g., downsampled mini-chunk or real chunk).
     *
     * @param blockData   The IBlockProvider to read from (can be a Chunk or a MiniChunk, etc.)
     * @param cx, cy, cz  The chunk coordinates if needed for neighbor checks;
     *                    otherwise (0,0,0) if you skip neighbors in LOD.
     * @param outVertices Output array for final merged vertices
     * @param outIndices  Output array for final merged indices
     * @param offsetX/Y/Z The base world position offset for these blocks
     * @param manager     Typically used if you do neighbor lookups
     *
     * @return true if we generated any geometry, false if no faces
     */
    bool generateMesh(
        const IBlockProvider& blockData,
        int cx, int cy, int cz,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        int offsetX, int offsetY, int offsetZ,
        const ChunkManager& manager
    ) const;
};

#endif // GREEDY_MESHER_H
