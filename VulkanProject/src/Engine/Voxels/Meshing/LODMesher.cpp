// LODMesher.cpp

#include "LODMesher.h"
#include "Engine/Voxels/Chunk.h"
#include "Engine/Voxels/ChunkManager.h"
#include "Engine/Voxels/Meshing/NaiveMesher.h"  // or GreedyMesher if you prefer
#include <algorithm>  // For std::min, std::max, etc.

// --------------------------------------------------------
// buildAllLODs
// --------------------------------------------------------
MultiLODResult LODMesher::buildAllLODs(
    Chunk& chunk,
    int cx, int cy, int cz,
    const IMesher* baseMesher,
    const ChunkManager& manager)
{
    MultiLODResult result;
    result.chunkPtr = &chunk;
    result.cx = cx;
    result.cy = cy;
    result.cz = cz;

    // 1) LOD0 => the real mesher
    {
        std::vector<Vertex> verts;
        std::vector<uint32_t> inds;

        int offX = cx * Chunk::SIZE_X;
        int offY = cy * Chunk::SIZE_Y;
        int offZ = cz * Chunk::SIZE_Z;

        baseMesher->generateMesh(
            chunk,
            cx, cy, cz,
            verts, inds,
            offX, offY, offZ,
            manager
        );

        result.lods[0].verts = std::move(verts);
        result.lods[0].inds = std::move(inds);
    }

    // 2) For LOD1..N => downsample + mesh
    //    In this example, we set up to result.MAX_LODS - 1
    for (int lodLevel = 1; lodLevel < MultiLODResult::MAX_LODS; lodLevel++)
    {
        std::vector<int> coarseVoxels;
        downsampleChunkData(chunk, (1 << lodLevel), coarseVoxels);

        // The coarser dimensions
        int cSizeX = Chunk::SIZE_X / (1 << lodLevel);
        int cSizeY = Chunk::SIZE_Y / (1 << lodLevel);
        int cSizeZ = Chunk::SIZE_Z / (1 << lodLevel);

        std::vector<Vertex> verts;
        std::vector<uint32_t> inds;

        int offX = cx * Chunk::SIZE_X;
        int offY = cy * Chunk::SIZE_Y;
        int offZ = cz * Chunk::SIZE_Z;

        meshDownsampledData(
            coarseVoxels, cSizeX, cSizeY, cSizeZ,
            offX, offY, offZ,
            verts, inds
        );

        result.lods[lodLevel].verts = std::move(verts);
        result.lods[lodLevel].inds = std::move(inds);
    }

    return result;
}

// --------------------------------------------------------
// downsampleChunkData
// --------------------------------------------------------
void LODMesher::downsampleChunkData(
    const Chunk& src,
    int factor,
    std::vector<int>& outVoxels)
{
    // factor=2 means each 2x2x2 block in the chunk becomes 1 voxel in the result
    // chunk dimension => e.g. 16 x 128 x 16
    // cSizeX => 16/2=8, cSizeY=>128/2=64, cSizeZ=>16/2=8
    const int cSizeX = Chunk::SIZE_X / factor;
    const int cSizeY = Chunk::SIZE_Y / factor;
    const int cSizeZ = Chunk::SIZE_Z / factor;

    outVoxels.resize(cSizeX * cSizeY * cSizeZ, 0);

    for (int zz = 0; zz < cSizeZ; zz++)
    {
        for (int yy = 0; yy < cSizeY; yy++)
        {
            for (int xx = 0; xx < cSizeX; xx++)
            {
                // The NxNxN region in the original chunk
                int startX = xx * factor;
                int startY = yy * factor;
                int startZ = zz * factor;

                // For this example, we pick the first non-air block we find:
                // (You could also do majority, or an average, or topmost.)
                int chosenID = 0; // 0 => air
                bool foundSolid = false;

                for (int dz = 0; dz < factor && !foundSolid; dz++)
                {
                    for (int dy = 0; dy < factor && !foundSolid; dy++)
                    {
                        for (int dx = 0; dx < factor; dx++)
                        {
                            int realX = startX + dx;
                            int realY = startY + dy;
                            int realZ = startZ + dz;

                            int id = src.getBlock(realX, realY, realZ);
                            if (id != 0) // if not air
                            {
                                chosenID = id;
                                foundSolid = true;
                                break;
                            }
                        }
                    }
                }

                size_t outIndex = (zz * cSizeY + yy) * cSizeX + xx;
                outVoxels[outIndex] = chosenID;
            }
        }
    }
}

// --------------------------------------------------------
// meshDownsampledData
// --------------------------------------------------------
void LODMesher::meshDownsampledData(
    const std::vector<int>& coarseVoxels,
    int coarseSizeX,
    int coarseSizeY,
    int coarseSizeZ,
    int offsetX,
    int offsetY,
    int offsetZ,
    std::vector<Vertex>& outVerts,
    std::vector<uint32_t>& outIndices)
{
    // Super-simple naive approach: 
    // For each voxel !=0 => build a tiny 1x1x1 cube with the usual 6 faces (like a mini naive mesher).
    outVerts.clear();
    outIndices.clear();

    auto getVoxel = [&](int x, int y, int z)->int
        {
            if (x < 0 || x >= coarseSizeX ||
                y < 0 || y >= coarseSizeY ||
                z < 0 || z >= coarseSizeZ) return 0;
            return coarseVoxels[(z * coarseSizeY + y) * coarseSizeX + x];
        };

    // We'll assign a basic color or use the ID as color factor. 
    // You might do a more advanced approach if you want the original VoxelType colors.
    for (int z = 0; z < coarseSizeZ; z++)
    {
        for (int y = 0; y < coarseSizeY; y++)
        {
            for (int x = 0; x < coarseSizeX; x++)
            {
                int id = getVoxel(x, y, z);
                if (id == 0) continue; // air => skip

                // Simple color from ID (just an example)
                float r = ((id * 53) % 255) / 255.f;
                float g = ((id * 97) % 255) / 255.f;
                float b = ((id * 199) % 255) / 255.f;

                // "World space" = offset + (x,y,z)
                float bx = float(offsetX + x);
                float by = float(offsetY + y);
                float bz = float(offsetZ + z);

                // For each of the 6 faces, if neighbor is air => add face
                // This is exactly like a small naive mesher approach.

                auto addQuad = [&](float x0, float y0, float z0,
                    float x1, float y1, float z1,
                    float x2, float y2, float z2,
                    float x3, float y3, float z3)
                    {
                        uint32_t startIndex = (uint32_t)outVerts.size();
                        outVerts.push_back(Vertex(x0, y0, z0, r, g, b));
                        outVerts.push_back(Vertex(x1, y1, z1, r, g, b));
                        outVerts.push_back(Vertex(x2, y2, z2, r, g, b));
                        outVerts.push_back(Vertex(x3, y3, z3, r, g, b));

                        outIndices.push_back(startIndex + 0);
                        outIndices.push_back(startIndex + 1);
                        outIndices.push_back(startIndex + 2);
                        outIndices.push_back(startIndex + 2);
                        outIndices.push_back(startIndex + 3);
                        outIndices.push_back(startIndex + 0);
                    };

                // +X neighbor check
                if (getVoxel(x + 1, y, z) == 0) {
                    addQuad(
                        bx + 1, by, bz, bx + 1, by, bz + 1,
                        bx + 1, by + 1, bz + 1, bx + 1, by + 1, bz
                    );
                }
                // -X
                if (getVoxel(x - 1, y, z) == 0) {
                    // reversed winding
                    addQuad(
                        bx, by, bz + 1, bx, by, bz,
                        bx, by + 1, bz, bx, by + 1, bz + 1
                    );
                }
                // +Y
                if (getVoxel(x, y + 1, z) == 0) {
                    addQuad(
                        bx, by + 1, bz, bx + 1, by + 1, bz,
                        bx + 1, by + 1, bz + 1, bx, by + 1, bz + 1
                    );
                }
                // -Y
                if (getVoxel(x, y - 1, z) == 0) {
                    addQuad(
                        bx + 1, by, bz, bx, by, bz,
                        bx, by, bz + 1, bx + 1, by, bz + 1
                    );
                }
                // +Z
                if (getVoxel(x, y, z + 1) == 0) {
                    addQuad(
                        bx, by, bz + 1, bx + 1, by, bz + 1,
                        bx + 1, by + 1, bz + 1, bx, by + 1, bz + 1
                    );
                }
                // -Z
                if (getVoxel(x, y, z - 1) == 0) {
                    addQuad(
                        bx + 1, by, bz, bx, by, bz,
                        bx, by + 1, bz, bx + 1, by + 1, bz
                    );
                }
            }
        }
    }
}
