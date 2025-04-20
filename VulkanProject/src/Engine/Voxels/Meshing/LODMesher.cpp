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

    // ─────────────────────────────────────────────────────────────────────────
    // LOD‑0: original path (unchanged)
    // ─────────────────────────────────────────────────────────────────────────
    if (lodLevel == 0) {
        std::vector<Vertex> fullVerts;
        std::vector<uint32_t> fullInds;

        int offX = cx * Chunk::SIZE_X;
        int offY = cy * Chunk::SIZE_Y;
        int offZ = cz * Chunk::SIZE_Z;

        mesher->generateMesh(chunk, cx, cy, cz,
            fullVerts, fullInds,
            offX, offY, offZ,
            manager);

        out.verts = std::move(fullVerts);
        out.inds = std::move(fullInds);
        out.geometricError = 0.f;
        return out;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // LOD ≥ 1  —  “solid‑any + dilate” coarse representation
    // ─────────────────────────────────────────────────────────────────────────
    const int stride = 1 << lodLevel;
    if (stride >= Chunk::SIZE_X || stride >= Chunk::SIZE_Y || stride >= Chunk::SIZE_Z) {
        out.geometricError = 0.f;
        return out;
    }

    const int miniX = std::max(1, Chunk::SIZE_X / stride);
    const int miniY = std::max(1, Chunk::SIZE_Y / stride);
    const int miniZ = std::max(1, Chunk::SIZE_Z / stride);

    const int baseOffX = cx * Chunk::SIZE_X;
    const int baseOffY = cy * Chunk::SIZE_Y;
    const int baseOffZ = cz * Chunk::SIZE_Z;

    MiniChunk mini(miniX, miniY, miniZ, baseOffX, baseOffY, baseOffZ);

    // --- Pass 1 : height‑span sampling -----------------------------------------
    for (int z = 0; z < miniZ; ++z)
        for (int y = 0; y < miniY; ++y)
            for (int x = 0; x < miniX; ++x) {

                const int sx = x * stride;
                const int sy = y * stride;
                const int sz = z * stride;

                const int ex = std::min(sx + stride, Chunk::SIZE_X);
                const int ey = std::min(sy + stride, Chunk::SIZE_Y);
                const int ez = std::min(sz + stride, Chunk::SIZE_Z);

                int minY = Chunk::SIZE_Y, maxY = -1;
                std::array<int, 256> hist{};   // assumes ≤256 voxel types
                for (int zz = sz; zz < ez; ++zz)
                    for (int yy = sy; yy < ey; ++yy)
                        for (int xx = sx; xx < ex; ++xx) {
                            int id = chunk.getBlock(xx, yy, zz);
                            if (id == 0) continue;
                            hist[id]++;
                            if (yy < minY) minY = yy;
                            if (yy > maxY) maxY = yy;
                        }

                if (maxY < 0) {
                    // all air
                    continue;
                }

                // pick most common non‑air id
                int bestID = 1;
                for (int id = 2; id < (int)hist.size(); ++id)
                    if (hist[id] > hist[bestID]) bestID = id;

                // fill vertical span in mini‑chunk space
                for (int yy = minY; yy <= maxY; ++yy) {
                    int localY = (yy - sy) / stride;      // 0..(miniY‑1) in this cell
                    mini.setBlock(x, localY, z, bestID);
                }
            }

    // no dilation needed now – cells touch vertically
    // ---- HORIZONTAL dilation (one 6‑neighbour pass) ----
    MiniChunk filled = mini;                // copy previous result
    const int dirs[4][2] = { {1,0},{-1,0},{0,1},{0,-1} }; // XZ only

    for (int z = 0; z < miniZ; ++z)
        for (int y = 0; y < miniY; ++y)
            for (int x = 0; x < miniX; ++x) {
                if (mini.getBlock(x, y, z) != 0) continue; // already solid

                for (auto d : dirs) {
                    int nx = x + d[0], nz = z + d[1];
                    if (nx < 0 || nz < 0 || nx >= miniX || nz >= miniZ) continue;

                    int nb = mini.getBlock(nx, y, nz);
                    if (nb != 0) {               // neighbour solid → fill this cell
                        filled.setBlock(x, y, z, nb);
                        break;
                    }
                }
            }

    // from here on, use 'filled' instead of 'mini'
    std::vector<Vertex> lodVerts;
    std::vector<uint32_t> lodInds;

    s_greedyMesherForLOD.generateMesh(filled,    // <‑‑ use merged chunk
        0, 0, 0,
        lodVerts, lodInds,
        baseOffX, baseOffY, baseOffZ,
        manager);

    out.verts = std::move(lodVerts);
    out.inds = std::move(lodInds);
    out.geometricError = (out.inds.empty()) ? 0.f
        : 1.f / (1.f + out.inds.size() / 3.f);
    return out;
}
