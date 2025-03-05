#include "NaiveMesher.h"
#include "../Chunk.h"
#include "../ChunkManager.h"
#include "../VoxelTypeRegistry.h"
#include "../VoxelType.h"
#include "../ChunkMesher.h"
#include <vector>
#include <cmath>

// Helper function: check if the given voxel ID corresponds to a solid voxel.
static bool isSolidID(int voxelID) {
    if (voxelID < 0)
        return false;
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
    return vt.isSolid;
}

// Helper function: checks if the voxel at global coordinates is solid.
// If the coordinates are out-of-bounds in the current chunk, it checks the neighboring chunk.
static bool isSolidGlobal(
    const Chunk& currentChunk,
    int cx, int cy, int cz,
    int x, int y, int z,
    const ChunkManager& manager)
{
    if (x >= 0 && x < Chunk::SIZE_X &&
        y >= 0 && y < Chunk::SIZE_Y &&
        z >= 0 && z < Chunk::SIZE_Z)
    {
        int id = currentChunk.getBlock(x, y, z);
        return (id > 0) ? isSolidID(id) : false;
    }
    else {
        int nx = cx, ny = cy, nz = cz;
        int localX = x, localY = y, localZ = z;
        if (x < 0) { nx -= 1; localX += Chunk::SIZE_X; }
        else if (x >= Chunk::SIZE_X) { nx += 1; localX -= Chunk::SIZE_X; }
        if (y < 0) { ny -= 1; localY += Chunk::SIZE_Y; }
        else if (y >= Chunk::SIZE_Y) { ny += 1; localY -= Chunk::SIZE_Y; }
        if (z < 0) { nz -= 1; localZ += Chunk::SIZE_Z; }
        else if (z >= Chunk::SIZE_Z) { nz += 1; localZ -= Chunk::SIZE_Z; }

        const Chunk* neighbor = manager.getChunk(nx, ny, nz);
        if (!neighbor)
            return false;
        int id = neighbor->getBlock(localX, localY, localZ);
        return (id > 0) ? isSolidID(id) : false;
    }
}

bool NaiveMesher::generateMesh(
    Chunk& chunk,
    int cx, int cy, int cz,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    int offsetX, int offsetY, int offsetZ,
    const ChunkManager& manager) const
{
    outVertices.clear();
    outIndices.clear();

    for (int x = 0; x < Chunk::SIZE_X; x++) {
        for (int y = 0; y < Chunk::SIZE_Y; y++) {
            for (int z = 0; z < Chunk::SIZE_Z; z++) {
                int voxelID = chunk.getBlock(x, y, z);
                if (voxelID <= 0)
                    continue; // Skip air voxels.

                const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
                float r = vt.color.r, g = vt.color.g, b = vt.color.b;

                float baseX = static_cast<float>(x + offsetX);
                float baseY = static_cast<float>(y + offsetY);
                float baseZ = static_cast<float>(z + offsetZ);

                // +X face
                if (!isSolidGlobal(chunk, cx, cy, cz, x + 1, y, z, manager)) {
                    int startIdx = static_cast<int>(outVertices.size());
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ, r, g, b));
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ + 1, r, g, b));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ + 1, r, g, b));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ, r, g, b));

                    outIndices.push_back(startIdx + 0);
                    outIndices.push_back(startIdx + 1);
                    outIndices.push_back(startIdx + 2);
                    outIndices.push_back(startIdx + 2);
                    outIndices.push_back(startIdx + 3);
                    outIndices.push_back(startIdx + 0);
                }
                // -X face
                if (!isSolidGlobal(chunk, cx, cy, cz, x - 1, y, z, manager)) {
                    int startIdx = static_cast<int>(outVertices.size());
                    outVertices.push_back(Vertex(baseX, baseY, baseZ + 1, r, g, b));
                    outVertices.push_back(Vertex(baseX, baseY, baseZ, r, g, b));
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ, r, g, b));
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ + 1, r, g, b));

                    outIndices.push_back(startIdx + 0);
                    outIndices.push_back(startIdx + 1);
                    outIndices.push_back(startIdx + 2);
                    outIndices.push_back(startIdx + 2);
                    outIndices.push_back(startIdx + 3);
                    outIndices.push_back(startIdx + 0);
                }
                // +Y face
                if (!isSolidGlobal(chunk, cx, cy, cz, x, y + 1, z, manager)) {
                    int startIdx = static_cast<int>(outVertices.size());
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ, r, g, b));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ, r, g, b));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ + 1, r, g, b));
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ + 1, r, g, b));

                    outIndices.push_back(startIdx + 0);
                    outIndices.push_back(startIdx + 1);
                    outIndices.push_back(startIdx + 2);
                    outIndices.push_back(startIdx + 2);
                    outIndices.push_back(startIdx + 3);
                    outIndices.push_back(startIdx + 0);
                }
                // -Y face
                if (!isSolidGlobal(chunk, cx, cy, cz, x, y - 1, z, manager)) {
                    int startIdx = static_cast<int>(outVertices.size());
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ, r, g, b));
                    outVertices.push_back(Vertex(baseX, baseY, baseZ, r, g, b));
                    outVertices.push_back(Vertex(baseX, baseY, baseZ + 1, r, g, b));
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ + 1, r, g, b));

                    outIndices.push_back(startIdx + 0);
                    outIndices.push_back(startIdx + 1);
                    outIndices.push_back(startIdx + 2);
                    outIndices.push_back(startIdx + 2);
                    outIndices.push_back(startIdx + 3);
                    outIndices.push_back(startIdx + 0);
                }
                // +Z face
                if (!isSolidGlobal(chunk, cx, cy, cz, x, y, z + 1, manager)) {
                    int startIdx = static_cast<int>(outVertices.size());
                    outVertices.push_back(Vertex(baseX, baseY, baseZ + 1, r, g, b));
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ + 1, r, g, b));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ + 1, r, g, b));
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ + 1, r, g, b));

                    outIndices.push_back(startIdx + 0);
                    outIndices.push_back(startIdx + 1);
                    outIndices.push_back(startIdx + 2);
                    outIndices.push_back(startIdx + 2);
                    outIndices.push_back(startIdx + 3);
                    outIndices.push_back(startIdx + 0);
                }
                // -Z face
                if (!isSolidGlobal(chunk, cx, cy, cz, x, y, z - 1, manager)) {
                    int startIdx = static_cast<int>(outVertices.size());
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ, r, g, b));
                    outVertices.push_back(Vertex(baseX, baseY, baseZ, r, g, b));
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ, r, g, b));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ, r, g, b));

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

    return !outVertices.empty();
}
