#pragma once

#include <vector>
#include <cstdint>
#include "Engine/Voxels/Meshing/IMesher.h"  // For the Vertex struct
#include "Engine/Voxels/Chunk.h"
#include "Engine/Voxels/ChunkManager.h"

/**
 * LODMeshData holds the geometry for a single LOD level:
 * - verts / inds: geometry buffers
 * - lodErrors: an array of float error metrics for each LOD (some approaches store multiple)
 * - geometricError: a single float representing the error of this LOD relative to higher detail
 */
struct LODMeshData
{
    std::vector<Vertex>   verts;
    std::vector<uint32_t> inds;

    // For storing an error metric per LOD index (if needed)
    std::vector<float>    lodErrors;

    // Overall geometric error for this LOD
    float geometricError = 0.f;
};

/**
 * MultiLODResult aggregates mesh data for multiple LOD levels belonging to one chunk:
 * - chunkPtr: pointer to the chunk we meshed
 * - cx, cy, cz: chunk world coordinates
 * - lods[]: up to N levels of geometry (e.g. LOD0, LOD1, LOD2, …)
 */
struct MultiLODResult
{
    // Which chunk these LODs belong to
    Chunk* chunkPtr = nullptr;

    // Optional world coords if you need them
    int cx = 0;
    int cy = 0;
    int cz = 0;

    // We support up to 8 LODs in this example
    static constexpr int MAX_LODS = 8;
    LODMeshData lods[MAX_LODS];
};

/**
 * LODMesher is responsible for building multiple LODs for a chunk.
 *
 * LOD0 is your original full-resolution mesh (via the IMesher).
 * LOD1, LOD2, etc. can be built by downsampling or some coarser approach.
 */
class LODMesher
{
public:
    /**
     * buildAllLODs:
     *   - Creates LOD0 using the provided IMesher (Greedy, Naive, etc.).
     *   - Builds higher LODs by downsampling or skipping some samples.
     * Returns a MultiLODResult containing geometry for every LOD level.
     */
    static MultiLODResult buildAllLODs(
        Chunk& chunk,
        int cx, int cy, int cz,
        const IMesher* baseMesher,
        const ChunkManager& manager
    );

    static LODMeshData buildLOD(Chunk& chunk, int cx, int cy, int cz, const IMesher* mesher, const ChunkManager& manager, int lodLevel);

private:
    /**
     * downsampleChunkData:
     *   - Reads the chunk’s voxel data and produces a coarser array
     *     (factor x factor x factor blocks become 1 voxel).
     *   - For factor=2, each 2x2x2 region merges into 1 voxel, etc.
     *     The merging logic can be majority, average, etc.
     */
    static void downsampleChunkData(
        const Chunk& src,
        int factor,
        std::vector<int>& outVoxels
    );

    /**
     * meshDownsampledData:
     *   - Given the coarser voxel array, produce geometry (verts+inds).
     *   - You can do a naive approach or reuse your mesher.
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
