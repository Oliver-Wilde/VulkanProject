#include "NaiveMesher.h"
#include "../Chunk.h"
#include "../ChunkManager.h"
#include "../VoxelTypeRegistry.h"
#include "../VoxelType.h"
#include "IMesher.h"
#include <vector>
#include <cmath>

// Helper function to pack float color into uint32_t RGBA8.
static uint32_t packColor(float r, float g, float b)
{
    // clamp
    if (r < 0.f) r = 0.f; if (r > 1.f) r = 1.f;
    if (g < 0.f) g = 0.f; if (g > 1.f) g = 1.f;
    if (b < 0.f) b = 0.f; if (b > 1.f) b = 1.f;

    uint32_t R = static_cast<uint32_t>(r * 255.0f);
    uint32_t G = static_cast<uint32_t>(g * 255.0f);
    uint32_t B = static_cast<uint32_t>(b * 255.0f);
    uint32_t A = 255; // full opacity

    // Store as 0xAABBGGRR by default
    // (The exact ordering is up to you, but your fragment shader should match.)
    return (A << 24) | (B << 16) | (G << 8) | (R << 0);
}

// Helper function: check if the given voxel ID is a solid voxel.
static bool isSolidID(int voxelID) {
    if (voxelID < 0)
        return false;
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
    return vt.isSolid;
}

// Helper: checks if the voxel at (x,y,z) is solid. If out-of-bounds, checks neighbor chunk.
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
        return (id > 0) && isSolidID(id);
    }

    int nx = cx, ny = cy, nz = cz;
    int lx = x, ly = y, lz = z;

    if (lx < 0) { nx--; lx += Chunk::SIZE_X; }
    else if (lx >= Chunk::SIZE_X) { nx++; lx -= Chunk::SIZE_X; }
    if (ly < 0) { ny--; ly += Chunk::SIZE_Y; }
    else if (ly >= Chunk::SIZE_Y) { ny++; ly -= Chunk::SIZE_Y; }
    if (lz < 0) { nz--; lz += Chunk::SIZE_Z; }
    else if (lz >= Chunk::SIZE_Z) { nz++; lz -= Chunk::SIZE_Z; }

    std::shared_ptr<const Chunk> neigh = manager.getChunk(nx, ny, nz);
    if (!neigh) return false;

    int id = neigh->getBlock(lx, ly, lz);
    return (id > 0) && isSolidID(id);
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
                    continue; // Skip air.

                // Grab the voxel color
                const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
                float r = vt.color.r, g = vt.color.g, b = vt.color.b;
                uint32_t packedColor = packColor(r, g, b);

                float baseX = static_cast<float>(x + offsetX);
                float baseY = static_cast<float>(y + offsetY);
                float baseZ = static_cast<float>(z + offsetZ);

                // +X face
                if (!isSolidGlobal(chunk, cx, cy, cz, x + 1, y, z, manager)) {
                    int startIdx = static_cast<int>(outVertices.size());
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ, packedColor));
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ + 1, packedColor));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ + 1, packedColor));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ, packedColor));

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
                    outVertices.push_back(Vertex(baseX, baseY, baseZ + 1, packedColor));
                    outVertices.push_back(Vertex(baseX, baseY, baseZ, packedColor));
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ, packedColor));
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ + 1, packedColor));

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
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ, packedColor));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ, packedColor));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ + 1, packedColor));
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ + 1, packedColor));

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
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ, packedColor));
                    outVertices.push_back(Vertex(baseX, baseY, baseZ, packedColor));
                    outVertices.push_back(Vertex(baseX, baseY, baseZ + 1, packedColor));
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ + 1, packedColor));

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
                    outVertices.push_back(Vertex(baseX, baseY, baseZ + 1, packedColor));
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ + 1, packedColor));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ + 1, packedColor));
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ + 1, packedColor));

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
                    outVertices.push_back(Vertex(baseX + 1, baseY, baseZ, packedColor));
                    outVertices.push_back(Vertex(baseX, baseY, baseZ, packedColor));
                    outVertices.push_back(Vertex(baseX, baseY + 1, baseZ, packedColor));
                    outVertices.push_back(Vertex(baseX + 1, baseY + 1, baseZ, packedColor));

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
