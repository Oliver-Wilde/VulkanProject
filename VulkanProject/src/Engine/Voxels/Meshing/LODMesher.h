#pragma once

#include <vector>
#include <cstdint>
#include "Engine/Voxels/Meshing/IMesher.h"  // For the Vertex struct
#include "Engine/Voxels/Chunk.h"
#include "Engine/Voxels/ChunkManager.h"

/**
 * LODMeshData holds the geometry for a single LOD level:
 * - verts: the vertex array
 * - inds : the index array
 */
struct LODMeshData
{
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
};

/**
 * MultiLODResult aggregates mesh data for multiple LOD levels belonging to one chunk:
 * - chunkPtr: pointer to the chunk we meshed
 * - cx, cy, cz: chunk world coordinates
 * - lods[]: up to N levels of geometry (e.g. LOD0, LOD1, LOD2, …)
 */
struct MultiLODResult
{
    Chunk* chunkPtr = nullptr;
    int cx = 0;
    int cy = 0;
    int cz = 0;

    static constexpr int MAX_LODS = 3;  // Change as desired
    LODMeshData lods[MAX_LODS];
};

/**
 * LODMesher is responsible for building multiple LODs for a chunk.
 *
 * LOD0 is your original full-resolution mesh, which uses your existing IMesher.
 * LOD1, LOD2, etc. are built by a downsampling step and a simpler meshing pass.
 */
class LODMesher
{
public:
    /**
     * buildAllLODs:
     *   - Creates LOD0 using the provided IMesher (Greedy, Naive, etc.).
     *   - For each higher LOD (1..N), downsamples chunk voxel data and meshes that coarser data.
     * Returns a MultiLODResult containing geometry for every LOD level.
     */
    static MultiLODResult buildAllLODs(
        Chunk& chunk,
        int cx, int cy, int cz,
        const IMesher* baseMesher,
        const ChunkManager& manager
    );

private:
    /**
     * downsampleChunkData:
     *   - Reads the chunk’s voxel data and produces a coarser “factor x factor” downsampled array.
     *   - For example, factor=2 means each 2x2x2 block in the original chunk is combined into 1 voxel.
     *     The logic for “combining” (majority, average, etc.) is up to you.
     */
    static void downsampleChunkData(
        const Chunk& src,
        int factor,
        std::vector<int>& outVoxels
    );

    /**
     * meshDownsampledData:
     *   - Given a coarser voxel array (dimensions: sizeX=size/factor, etc.), produce geometry.
     *   - You can do a naive “cube for each voxel” approach, or reuse your IMesher by faking a chunk.
     */
    static void meshDownsampledData(
        const std::vector<int>& coarseVoxels,
        int coarseSizeX,
        int coarseSizeY,
        int coarseSizeZ,
        int offsetX,
        int offsetY,
        int offsetZ,
        std::vector<Vertex>& outVerts,
        std::vector<uint32_t>& outIndices
    );
};
