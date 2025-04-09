#include "LODMesher.h"
#include "../Chunk.h"
#include "../ChunkManager.h"
#include "../VoxelTypeRegistry.h"
#include "../VoxelType.h"
#include "IMesher.h"         // For the user-chosen mesher (Naive or Greedy) at LOD=0
#include "GreedyMesher.h"    // Forced greedy merges at LOD≥1
#include "Engine/Voxels/MiniChunk.h"  // Our IBlockProvider-based mini-chunk

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <unordered_map>

// A dedicated GreedyMesher for LOD usage:
static GreedyMesher s_greedyMesherForLOD;

MultiLODResult LODMesher::buildAllLODs(
    Chunk& chunk,
    int cx, int cy, int cz,
    const IMesher* mesher,
    const ChunkManager& manager)
{
    MultiLODResult result;
    result.chunkPtr = &chunk;

    constexpr int maxLods = MultiLODResult::MAX_LODS;
    for (int lodLevel = 0; lodLevel < maxLods; lodLevel++)
    {
        // Build each LOD
        LODMeshData data = buildLOD(chunk, cx, cy, cz, mesher, manager, lodLevel);

        // Move geometry + store error
        result.lods[lodLevel].verts = std::move(data.verts);
        result.lods[lodLevel].inds = std::move(data.inds);
        result.lods[lodLevel].lodErrors.resize(maxLods);
        result.lods[lodLevel].lodErrors[lodLevel] = data.geometricError;
    }

    return result;
}

LODMeshData LODMesher::buildLOD(
    Chunk& chunk,
    int cx, int cy, int cz,
    const IMesher* mesher,
    const ChunkManager& manager,
    int lodLevel)
{
    LODMeshData out;

    // ----------------------------------------------------------------------------
    // LOD=0 => use whichever mesher the user selected (Naive or Greedy)
    // ----------------------------------------------------------------------------
    if (lodLevel == 0)
    {
        std::vector<Vertex> fullVerts;
        std::vector<uint32_t> fullInds;

        // World-space offset
        int offX = cx * Chunk::SIZE_X;
        int offY = cy * Chunk::SIZE_Y;
        int offZ = cz * Chunk::SIZE_Z;

        mesher->generateMesh(
            chunk,             // chunk data
            cx, cy, cz,        // chunk coords (for neighbor logic if needed)
            fullVerts, fullInds,
            offX, offY, offZ,
            manager
        );

        out.verts = std::move(fullVerts);
        out.inds = std::move(fullInds);
        out.geometricError = 0.0f; // no "error" at highest LOD
        return out;
    }

    // ----------------------------------------------------------------------------
    // LOD≥1 => "majority block" approach in a smaller MiniChunk, then GreedyMesher
    // ----------------------------------------------------------------------------
    int stride = (1 << lodLevel);

    // If stride is too large in any dimension, we skip
    if (stride >= Chunk::SIZE_X ||
        stride >= Chunk::SIZE_Y ||
        stride >= Chunk::SIZE_Z)
    {
        out.geometricError = 0.0f;
        return out;
    }

    // Calculate the mini-chunk size in X, Y, Z
    int miniX = std::max(1, Chunk::SIZE_X / stride);
    int miniY = std::max(1, Chunk::SIZE_Y / stride);
    int miniZ = std::max(1, Chunk::SIZE_Z / stride);

    // Base offset in world coords
    int baseOffX = cx * Chunk::SIZE_X;
    int baseOffY = cy * Chunk::SIZE_Y;
    int baseOffZ = cz * Chunk::SIZE_Z;

    // Create our mini-chunk
    MiniChunk mini(miniX, miniY, miniZ, baseOffX, baseOffY, baseOffZ);

    // For each cell in the mini-chunk, pick the most frequent non-air block
    // from the corresponding stride×stride×stride region in the real chunk.
    for (int z = 0; z < miniZ; z++)
    {
        for (int y = 0; y < miniY; y++)
        {
            for (int x = 0; x < miniX; x++)
            {
                int startX = x * stride;
                int startY = y * stride; // crucial to downsample vertically
                int startZ = z * stride;

                int endX = std::min(startX + stride, Chunk::SIZE_X);
                int endY = std::min(startY + stride, Chunk::SIZE_Y);
                int endZ = std::min(startZ + stride, Chunk::SIZE_Z);

                std::unordered_map<int, int> freq;
                freq.reserve((endX - startX) * (endY - startY) * (endZ - startZ));

                for (int zSub = startZ; zSub < endZ; zSub++)
                {
                    for (int ySub = startY; ySub < endY; ySub++)
                    {
                        for (int xSub = startX; xSub < endX; xSub++)
                        {
                            int blockID = chunk.getBlock(xSub, ySub, zSub);
                            freq[blockID]++;
                        }
                    }
                }

                // Highest frequency among non-air
                int bestBlock = 0;
                int bestCount = freq[0]; // freq of air
                for (auto& kv : freq)
                {
                    int bID = kv.first;
                    int count = kv.second;
                    if (bID != 0 && count > bestCount)
                    {
                        bestBlock = bID;
                        bestCount = count;
                    }
                }

                mini.setBlock(x, y, z, bestBlock);
            }
        }
    }

    // Now mesh the mini-chunk with a forced GreedyMesher
    std::vector<Vertex> lodVerts;
    std::vector<uint32_t> lodInds;

    s_greedyMesherForLOD.generateMesh(
        mini,    // IBlockProvider
        0, 0, 0, // chunk coords not used in LOD≥1
        lodVerts, lodInds,
        baseOffX, baseOffY, baseOffZ,
        manager
    );

    out.verts = std::move(lodVerts);
    out.inds = std::move(lodInds);

    // Optional "geometric error" estimate => 1 / (1 + triCount)
    float triCount = float(out.inds.size()) / 3.0f;
    out.geometricError = 1.0f / (1.0f + triCount);

    return out;
}
