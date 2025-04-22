#include "LODMesher.h"
#include "../Chunk.h"
#include "../ChunkManager.h"
#include "../VoxelTypeRegistry.h"
#include "../VoxelType.h"
#include "IMesher.h"         // user‑chosen mesher at LOD 0
#include "GreedyMesher.h"    // forced greedy merges at LOD ≥ 1
#include "Engine/Voxels/MiniChunk.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <unordered_map>

// dedicated GreedyMesher for coarse LODs
static GreedyMesher s_greedyMesherForLOD;

/* ========================================================================== */
/* buildAllLODs – master entry                                                */
/* ========================================================================== */
MultiLODResult LODMesher::buildAllLODs(
    Chunk& chunk, int cx, int cy, int cz,
    const IMesher* mesher,
    const ChunkManager& manager)
{
    MultiLODResult r;
    r.chunkPtr = &chunk;

    for (int lod = 0; lod < MultiLODResult::MAX_LODS; ++lod)
    {
        LODMeshData d = buildLOD(chunk, cx, cy, cz, mesher, manager, lod);
        r.lods[lod].verts = std::move(d.verts);
        r.lods[lod].inds = std::move(d.inds);
        r.lods[lod].lodErrors.resize(MultiLODResult::MAX_LODS);
        r.lods[lod].lodErrors[lod] = d.geometricError;
    }
    return r;
}

/* ========================================================================== */
/* buildLOD – LOD 0 through N                                                 */
/* ========================================================================== */
LODMeshData LODMesher::buildLOD(
    Chunk& chunk, int cx, int cy, int cz,
    const IMesher* mesher,
    const ChunkManager& manager,
    int lodLevel)
{
    LODMeshData out;

    /* ── LOD 0 : full‑resolution chunk via user mesher ──────────────────── */
    if (lodLevel == 0)
    {
        int ox = cx * Chunk::SIZE_X;
        int oy = cy * Chunk::SIZE_Y;
        int oz = cz * Chunk::SIZE_Z;

        mesher->generateMesh(chunk, cx, cy, cz,
            out.verts, out.inds,
            ox, oy, oz,
            manager);
        out.geometricError = 0.f;
        return out;
    }

    /* ── LOD ≥ 1 : down‑sample + greedy merge ───────────────────────────── */
    const int stride = 1 << lodLevel;
    if (stride >= Chunk::SIZE_X ||
        stride >= Chunk::SIZE_Y ||
        stride >= Chunk::SIZE_Z)
    {
        out.geometricError = 0.f;
        return out;
    }

    const int miniX = std::max(1, Chunk::SIZE_X / stride);
    const int miniY = std::max(1, Chunk::SIZE_Y / stride);
    const int miniZ = std::max(1, Chunk::SIZE_Z / stride);

    const int baseX = cx * Chunk::SIZE_X;
    const int baseY = cy * Chunk::SIZE_Y;
    const int baseZ = cz * Chunk::SIZE_Z;

    MiniChunk mini(miniX, miniY, miniZ, baseX, baseY, baseZ);

    /* Pass 1 — height‑span sampling inside each stride cell */
    for (int z = 0; z < miniZ; ++z)
        for (int y = 0; y < miniY; ++y)
            for (int x = 0; x < miniX; ++x)
            {
                int sx = x * stride, sy = y * stride, sz = z * stride;
                int ex = std::min(sx + stride, Chunk::SIZE_X);
                int ey = std::min(sy + stride, Chunk::SIZE_Y);
                int ez = std::min(sz + stride, Chunk::SIZE_Z);

                int minY = Chunk::SIZE_Y, maxY = -1;
                std::array<int, 256> hist{};
                for (int zz = sz; zz < ez; ++zz)
                    for (int yy = sy; yy < ey; ++yy)
                        for (int xx = sx; xx < ex; ++xx)
                        {
                            int id = chunk.getBlock(xx, yy, zz);
                            if (id == 0) continue;
                            hist[id]++;
                            minY = std::min(minY, yy);
                            maxY = std::max(maxY, yy);
                        }

                if (maxY < 0) continue;   // all air

                int best = 1;
                for (int id = 2; id < (int)hist.size(); ++id)
                    if (hist[id] > hist[best]) best = id;

                for (int yy = minY; yy <= maxY; ++yy)
                {
                    int ly = (yy - sy) / stride;
                    mini.setBlock(x, ly, z, best);
                }
            }

    /* Pass 2 — single horizontal dilation pass */
    MiniChunk filled = mini;
    const int dirs[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };

    for (int z = 0; z < miniZ; ++z)
        for (int y = 0; y < miniY; ++y)
            for (int x = 0; x < miniX; ++x)
            {
                if (mini.getBlock(x, y, z) != 0) continue;
                for (auto d : dirs)
                {
                    int nx = x + d[0], nz = z + d[1];
                    if (nx < 0 || nz < 0 || nx >= miniX || nz >= miniZ) continue;
                    int nb = mini.getBlock(nx, y, nz);
                    if (nb != 0) { filled.setBlock(x, y, z, nb); break; }
                }
            }

    /* Greedy mesh the filled mini‑chunk */
    s_greedyMesherForLOD.generateMesh(filled, 0, 0, 0,
        out.verts, out.inds,
        baseX, baseY, baseZ,
        manager);

    out.geometricError = out.inds.empty() ? 0.f
        : 1.f / (1.f + out.inds.size() / 3.f);
    return out;
}
