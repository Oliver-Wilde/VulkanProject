#include "LODMesher.h"
#include "../Chunk.h"
#include "../ChunkManager.h"
#include "../VoxelTypeRegistry.h"
#include "../VoxelType.h"
#include "IMesher.h"
#include "NaiveMesher.h"
#include "GreedyMesher.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

// ---------------------------------------------------------------
// buildAllLODs
// => We’ll build up to MAX_LODS, each at half resolution in XZ
//    for LOD1 and beyond. LOD0 uses the real mesher (greedy).
// ---------------------------------------------------------------
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
        // Build geometry for this LOD
        LODMeshData data = buildLOD(chunk, cx, cy, cz, mesher, manager, lodLevel);

        // Transfer geometry into result
        result.lods[lodLevel].verts = std::move(data.verts);
        result.lods[lodLevel].inds = std::move(data.inds);
        result.lods[lodLevel].lodErrors.resize(maxLods); // track error in same container
        result.lods[lodLevel].lodErrors[lodLevel] = data.geometricError;
    }

    return result;
}

// ---------------------------------------------------------------
// buildLOD => LOD0 uses the real mesher (Greedy, etc.),
//             LOD>0 does a naive downsample approach.
//
// Explanation:
//   - If lodLevel == 0, call mesher->generateMesh(...) directly on the chunk
//   - If lodLevel >= 1, we do the simplified approach with stride in XZ
// ---------------------------------------------------------------
LODMeshData LODMesher::buildLOD(
    Chunk& chunk,
    int cx, int cy, int cz,
    const IMesher* mesher,
    const ChunkManager& manager,
    int lodLevel)
{
    LODMeshData out;

    // If LOD=0 => call the real mesher (Greedy or Naive)
    if (lodLevel == 0)
    {
        // We'll just do a full mesh using the provided "mesher"
        // (GreedyMesher if you set it up that way).
        std::vector<Vertex> fullVerts;
        std::vector<uint32_t> fullInds;

        // We'll compute offsets to place geometry in world space
        int offX = cx * Chunk::SIZE_X;
        int offY = cy * Chunk::SIZE_Y;
        int offZ = cz * Chunk::SIZE_Z;

        mesher->generateMesh(
            chunk,
            cx, cy, cz,
            fullVerts, fullInds,
            offX, offY, offZ,
            manager
        );

        // Fill out the LODMeshData
        out.verts = std::move(fullVerts);
        out.inds = std::move(fullInds);
        out.geometricError = 0.0f; // highest LOD => no error
        return out;
    }

    // Otherwise, for LOD>=1, let's do the down-sample approach
    const int stride = 1 << lodLevel;  // 2,4,8,...

    // If stride >= chunk size, produce no geometry
    if (stride >= Chunk::SIZE_X || stride >= Chunk::SIZE_Z)
    {
        out.geometricError = 0.0f;
        return out;
    }

    // Build a "mini-chunk" by downsampling
    int miniX = std::max(1, Chunk::SIZE_X / stride);
    int miniZ = std::max(1, Chunk::SIZE_Z / stride);
    int miniY = Chunk::SIZE_Y; // keep Y same for demonstration

    std::vector<int> downsampled;
    downsampled.resize(size_t(miniX * miniY * miniZ), 0);

    for (int z = 0; z < miniZ; z++)
    {
        for (int y = 0; y < miniY; y++)
        {
            for (int x = 0; x < miniX; x++)
            {
                int srcX = x * stride;
                int srcZ = z * stride;
                int id = chunk.getBlock(srcX, y, srcZ);

                size_t idx = size_t(z) * miniY * miniX
                    + size_t(y) * miniX
                    + size_t(x);
                downsampled[idx] = id;
            }
        }
    }

    // Create geometry from the downsample data
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;

    int baseOffX = cx * Chunk::SIZE_X;
    int baseOffY = cy * Chunk::SIZE_Y;
    int baseOffZ = cz * Chunk::SIZE_Z;

    // Basic approach: For each voxel in mini-chunk, check neighbors => add faces
    auto getDownsampledBlock = [&](int xx, int yy, int zz) -> int
        {
            if (xx < 0 || xx >= miniX || yy < 0 || yy >= miniY || zz < 0 || zz >= miniZ)
                return -1;
            size_t idx = size_t(zz) * miniY * miniX + size_t(yy) * miniX + size_t(xx);
            return downsampled[idx];
        };

    // For each voxel in mini-chunk
    for (int z = 0; z < miniZ; z++)
    {
        for (int y = 0; y < miniY; y++)
        {
            for (int x = 0; x < miniX; x++)
            {
                int voxelID = getDownsampledBlock(x, y, z);
                if (voxelID <= 0) continue; // skip air

                // Grab color from VoxelType
                const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
                float r = vt.color.r;
                float g = vt.color.g;
                float b = vt.color.b;

                float wx = float(baseOffX + x * stride);
                float wy = float(baseOffY + y);
                float wz = float(baseOffZ + z * stride);

                auto addQuad = [&](float x0, float y0, float z0,
                    float x1, float y1, float z1,
                    float x2, float y2, float z2,
                    float x3, float y3, float z3)
                    {
                        uint32_t startIdx = (uint32_t)verts.size();
                        verts.push_back(Vertex(x0, y0, z0, r, g, b));
                        verts.push_back(Vertex(x1, y1, z1, r, g, b));
                        verts.push_back(Vertex(x2, y2, z2, r, g, b));
                        verts.push_back(Vertex(x3, y3, z3, r, g, b));

                        inds.push_back(startIdx + 0);
                        inds.push_back(startIdx + 1);
                        inds.push_back(startIdx + 2);
                        inds.push_back(startIdx + 2);
                        inds.push_back(startIdx + 3);
                        inds.push_back(startIdx + 0);
                    };

                // +X
                if (x == miniX - 1 || getDownsampledBlock(x + 1, y, z) <= 0)
                {
                    addQuad(wx + stride, wy, wz,
                        wx + stride, wy, wz + stride,
                        wx + stride, wy + 1, wz + stride,
                        wx + stride, wy + 1, wz);
                }
                // -X
                if (x == 0 || getDownsampledBlock(x - 1, y, z) <= 0)
                {
                    addQuad(wx, wy, wz + stride,
                        wx, wy, wz,
                        wx, wy + 1, wz,
                        wx, wy + 1, wz + stride);
                }
                // +Y
                if (y == miniY - 1 || getDownsampledBlock(x, y + 1, z) <= 0)
                {
                    addQuad(wx, wy + 1, wz,
                        wx + stride, wy + 1, wz,
                        wx + stride, wy + 1, wz + stride,
                        wx, wy + 1, wz + stride);
                }
                // -Y
                if (y == 0 || getDownsampledBlock(x, y - 1, z) <= 0)
                {
                    addQuad(wx + stride, wy, wz,
                        wx, wy, wz,
                        wx, wy, wz + stride,
                        wx + stride, wy, wz + stride);
                }
                // +Z
                if (z == miniZ - 1 || getDownsampledBlock(x, y, z + 1) <= 0)
                {
                    addQuad(wx, wy, wz + stride,
                        wx + stride, wy, wz + stride,
                        wx + stride, wy + 1, wz + stride,
                        wx, wy + 1, wz + stride);
                }
                // -Z
                if (z == 0 || getDownsampledBlock(x, y, z - 1) <= 0)
                {
                    addQuad(wx + stride, wy, wz,
                        wx, wy, wz,
                        wx, wy + 1, wz,
                        wx + stride, wy + 1, wz);
                }
            }
        }
    }

    out.verts = std::move(verts);
    out.inds = std::move(inds);

    // Estimate geometric error for LOD>0
    float triCount = float(out.inds.size()) / 3.0f;
    out.geometricError = 1.0f / (1.0f + triCount);
    return out;
}
