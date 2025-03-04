#include "ChunkMesherNaive.h"

// Includes for everything referenced here:
#include "Chunk.h"
#include "ChunkManager.h"
#include "VoxelType.h"
#include "VoxelTypeRegistry.h"

#include <stdexcept>
#include <iostream>

namespace ChunkMesherNaive
{





    //----------------------------------------------
// isSolidID: checks if voxelID => a solid VoxelType
//----------------------------------------------
    bool isSolidID(int voxelID)
    {
        if (voxelID < 0) return false; // negative => treat as air
        const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
        return vt.isSolid;
    }
    //----------------------------------------------
// isSolidGlobal: checks if local coords (x,y,z)
// are solid, possibly in neighbor chunk
//----------------------------------------------
    bool isSolidGlobal(
        const Chunk& currentChunk,
        int cx, int cy, int cz,
        int x, int y, int z,
        const ChunkManager& manager)
    {
        // If in-range => check current chunk
        if (x >= 0 && x < Chunk::SIZE_X &&
            y >= 0 && y < Chunk::SIZE_Y &&
            z >= 0 && z < Chunk::SIZE_Z)
        {
            int id = currentChunk.getBlock(x, y, z);
            return (id > 0) ? isSolidID(id) : false;
        }
        else
        {
            // out-of-bounds => neighbor chunk
            int nx = cx, ny = cy, nz = cz;
            int localX = x, localY = y, localZ = z;

            if (x < 0) {
                nx -= 1;
                localX += Chunk::SIZE_X;
            }
            else if (x >= Chunk::SIZE_X) {
                nx += 1;
                localX -= Chunk::SIZE_X;
            }

            if (y < 0) {
                ny -= 1;
                localY += Chunk::SIZE_Y;
            }
            else if (y >= Chunk::SIZE_Y) {
                ny += 1;
                localY -= Chunk::SIZE_Y;
            }

            if (z < 0) {
                nz -= 1;
                localZ += Chunk::SIZE_Z;
            }
            else if (z >= Chunk::SIZE_Z) {
                nz += 1;
                localZ -= Chunk::SIZE_Z;
            }

            const Chunk* neighbor = manager.getChunk(nx, ny, nz);
            if (!neighbor) {
                // no neighbor => treat as air
                return false;
            }
            int blockID = neighbor->getBlock(localX, localY, localZ);
            return (blockID > 0) ? isSolidID(blockID) : false;
        }
    }

    /**
     * \brief Generate a mesh by checking each face of each voxel (the naive adjacency approach).
     *        This function references 'isSolidGlobal(...)', which must be accessible from here.
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
    )
    {
        outVertices.clear();
        outIndices.clear();

        for (int x = 0; x < Chunk::SIZE_X; x++)
        {
            for (int y = 0; y < Chunk::SIZE_Y; y++)
            {
                for (int z = 0; z < Chunk::SIZE_Z; z++)
                {
                    int voxelID = chunk.getBlock(x, y, z);
                    if (voxelID <= 0) continue; // skip air

                    // Grab the voxel's color
                    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
                    float r = vt.color.r;
                    float g = vt.color.g;
                    float b = vt.color.b;

                    float baseX = float(x + offsetX);
                    float baseY = float(y + offsetY);
                    float baseZ = float(z + offsetZ);

                    // +X face
                    if (!isSolidGlobal(chunk, cx, cy, cz, x + 1, y, z, manager))
                    {
                        int startIdx = static_cast<int>(outVertices.size());
                        outVertices.push_back(NVertex(baseX + 1, baseY, baseZ, r, g, b));
                        outVertices.push_back(NVertex(baseX + 1, baseY, baseZ + 1, r, g, b));
                        outVertices.push_back(NVertex(baseX + 1, baseY + 1, baseZ + 1, r, g, b));
                        outVertices.push_back(NVertex(baseX + 1, baseY + 1, baseZ, r, g, b));

                        outIndices.push_back(startIdx + 0);
                        outIndices.push_back(startIdx + 1);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 3);
                        outIndices.push_back(startIdx + 0);
                    }
                    // -X face
                    if (!isSolidGlobal(chunk, cx, cy, cz, x - 1, y, z, manager))
                    {
                        int startIdx = static_cast<int>(outVertices.size());
                        outVertices.push_back(NVertex(baseX, baseY, baseZ + 1, r, g, b));
                        outVertices.push_back(NVertex(baseX, baseY, baseZ, r, g, b));
                        outVertices.push_back(NVertex(baseX, baseY + 1, baseZ, r, g, b));
                        outVertices.push_back(NVertex(baseX, baseY + 1, baseZ + 1, r, g, b));

                        outIndices.push_back(startIdx + 0);
                        outIndices.push_back(startIdx + 1);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 3);
                        outIndices.push_back(startIdx + 0);
                    }
                    // +Y face
                    if (!isSolidGlobal(chunk, cx, cy, cz, x, y + 1, z, manager))
                    {
                        int startIdx = static_cast<int>(outVertices.size());
                        outVertices.push_back(NVertex(baseX, baseY + 1, baseZ, r, g, b));
                        outVertices.push_back(NVertex(baseX + 1, baseY + 1, baseZ, r, g, b));
                        outVertices.push_back(NVertex(baseX + 1, baseY + 1, baseZ + 1, r, g, b));
                        outVertices.push_back(NVertex(baseX, baseY + 1, baseZ + 1, r, g, b));

                        outIndices.push_back(startIdx + 0);
                        outIndices.push_back(startIdx + 1);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 3);
                        outIndices.push_back(startIdx + 0);
                    }
                    // -Y face
                    if (!isSolidGlobal(chunk, cx, cy, cz, x, y - 1, z, manager))
                    {
                        int startIdx = static_cast<int>(outVertices.size());
                        outVertices.push_back(NVertex(baseX + 1, baseY, baseZ, r, g, b));
                        outVertices.push_back(NVertex(baseX, baseY, baseZ, r, g, b));
                        outVertices.push_back(NVertex(baseX, baseY, baseZ + 1, r, g, b));
                        outVertices.push_back(NVertex(baseX + 1, baseY, baseZ + 1, r, g, b));

                        outIndices.push_back(startIdx + 0);
                        outIndices.push_back(startIdx + 1);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 3);
                        outIndices.push_back(startIdx + 0);
                    }
                    // +Z face
                    if (!isSolidGlobal(chunk, cx, cy, cz, x, y, z + 1, manager))
                    {
                        int startIdx = static_cast<int>(outVertices.size());
                        outVertices.push_back(NVertex(baseX, baseY, baseZ + 1, r, g, b));
                        outVertices.push_back(NVertex(baseX + 1, baseY, baseZ + 1, r, g, b));
                        outVertices.push_back(NVertex(baseX + 1, baseY + 1, baseZ + 1, r, g, b));
                        outVertices.push_back(NVertex(baseX, baseY + 1, baseZ + 1, r, g, b));

                        outIndices.push_back(startIdx + 0);
                        outIndices.push_back(startIdx + 1);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 3);
                        outIndices.push_back(startIdx + 0);
                    }
                    // -Z face
                    if (!isSolidGlobal(chunk, cx, cy, cz, x, y, z - 1, manager))
                    {
                        int startIdx = static_cast<int>(outVertices.size());
                        outVertices.push_back(NVertex(baseX + 1, baseY, baseZ, r, g, b));
                        outVertices.push_back(NVertex(baseX, baseY, baseZ, r, g, b));
                        outVertices.push_back(NVertex(baseX, baseY + 1, baseZ, r, g, b));
                        outVertices.push_back(NVertex(baseX + 1, baseY + 1, baseZ, r, g, b));

                        outIndices.push_back(startIdx + 0);
                        outIndices.push_back(startIdx + 1);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 3);
                        outIndices.push_back(startIdx + 0);
                    }
                }
            }
        }
    }

    /**
     * \brief A "test" method that places all 6 faces of each voxel, ignoring adjacency.
     */
    void generateMeshNaiveTest(
        const Chunk& chunk,
        std::vector<NVertex>& outVerts,
        std::vector<uint32_t>& outInds,
        int offsetX,
        int offsetY,
        int offsetZ
    )
    {
        outVerts.clear();
        outInds.clear();

        for (int x = 0; x < Chunk::SIZE_X; x++)
        {
            for (int y = 0; y < Chunk::SIZE_Y; y++)
            {
                for (int z = 0; z < Chunk::SIZE_Z; z++)
                {
                    int blockID = chunk.getBlock(x, y, z);
                    if (blockID <= 0) continue;

                    // Hard-coded color
                    float r = 0.5f;
                    float g = 0.5f;
                    float b = 0.5f;

                    float bx = float(x + offsetX);
                    float by = float(y + offsetY);
                    float bz = float(z + offsetZ);

                    // (+X)
                    {
                        int startIdx = static_cast<int>(outVerts.size());
                        outVerts.push_back(NVertex(bx + 1, by, bz, r, g, b));
                        outVerts.push_back(NVertex(bx + 1, by, bz + 1, r, g, b));
                        outVerts.push_back(NVertex(bx + 1, by + 1, bz + 1, r, g, b));
                        outVerts.push_back(NVertex(bx + 1, by + 1, bz, r, g, b));

                        outInds.push_back(startIdx + 0);
                        outInds.push_back(startIdx + 1);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 3);
                        outInds.push_back(startIdx + 0);
                    }
                    // (-X)
                    {
                        int startIdx = static_cast<int>(outVerts.size());
                        outVerts.push_back(NVertex(bx, by, bz + 1, r, g, b));
                        outVerts.push_back(NVertex(bx, by, bz, r, g, b));
                        outVerts.push_back(NVertex(bx, by + 1, bz, r, g, b));
                        outVerts.push_back(NVertex(bx, by + 1, bz + 1, r, g, b));

                        outInds.push_back(startIdx + 0);
                        outInds.push_back(startIdx + 1);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 3);
                        outInds.push_back(startIdx + 0);
                    }
                    // (+Y)
                    {
                        int startIdx = static_cast<int>(outVerts.size());
                        outVerts.push_back(NVertex(bx, by + 1, bz, r, g, b));
                        outVerts.push_back(NVertex(bx + 1, by + 1, bz, r, g, b));
                        outVerts.push_back(NVertex(bx + 1, by + 1, bz + 1, r, g, b));
                        outVerts.push_back(NVertex(bx, by + 1, bz + 1, r, g, b));

                        outInds.push_back(startIdx + 0);
                        outInds.push_back(startIdx + 1);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 3);
                        outInds.push_back(startIdx + 0);
                    }
                    // (-Y)
                    {
                        int startIdx = static_cast<int>(outVerts.size());
                        outVerts.push_back(NVertex(bx + 1, by, bz, r, g, b));
                        outVerts.push_back(NVertex(bx, by, bz, r, g, b));
                        outVerts.push_back(NVertex(bx, by, bz + 1, r, g, b));
                        outVerts.push_back(NVertex(bx + 1, by, bz + 1, r, g, b));

                        outInds.push_back(startIdx + 0);
                        outInds.push_back(startIdx + 1);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 3);
                        outInds.push_back(startIdx + 0);
                    }
                    // (+Z)
                    {
                        int startIdx = static_cast<int>(outVerts.size());
                        outVerts.push_back(NVertex(bx, by, bz + 1, r, g, b));
                        outVerts.push_back(NVertex(bx + 1, by, bz + 1, r, g, b));
                        outVerts.push_back(NVertex(bx + 1, by + 1, bz + 1, r, g, b));
                        outVerts.push_back(NVertex(bx, by + 1, bz + 1, r, g, b));

                        outInds.push_back(startIdx + 0);
                        outInds.push_back(startIdx + 1);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 3);
                        outInds.push_back(startIdx + 0);
                    }
                    // (-Z)
                    {
                        int startIdx = static_cast<int>(outVerts.size());
                        outVerts.push_back(NVertex(bx + 1, by, bz, r, g, b));
                        outVerts.push_back(NVertex(bx, by, bz, r, g, b));
                        outVerts.push_back(NVertex(bx, by + 1, bz, r, g, b));
                        outVerts.push_back(NVertex(bx + 1, by + 1, bz, r, g, b));

                        outInds.push_back(startIdx + 0);
                        outInds.push_back(startIdx + 1);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 2);
                        outInds.push_back(startIdx + 3);
                        outInds.push_back(startIdx + 0);
                    }
                }
            }
        }
    }

} // namespace ChunkMesherNaive
