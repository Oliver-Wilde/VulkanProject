#include "GreedyMesher.h"
#include "Engine/Voxels/IBlockProvider.h"
#include "../VoxelTypeRegistry.h"
#include "../VoxelType.h"
#include "../Chunk.h"
#include "../ChunkManager.h"

#include <vector>
#include <cstddef>
#include <stdexcept>
#include <algorithm> // for std::fill or other usage

// -----------------------------------------------------------------------------
// Helper: Pack (r,g,b) float [0..1] into a RGBA8 uint32_t
// -----------------------------------------------------------------------------
static uint32_t packColor(float r, float g, float b)
{
    if (r < 0.f) r = 0.f; if (r > 1.f) r = 1.f;
    if (g < 0.f) g = 0.f; if (g > 1.f) g = 1.f;
    if (b < 0.f) b = 0.f; if (b > 1.f) b = 1.f;

    uint32_t R = static_cast<uint32_t>(r * 255.0f);
    uint32_t G = static_cast<uint32_t>(g * 255.0f);
    uint32_t B = static_cast<uint32_t>(b * 255.0f);
    uint32_t A = 255; // full opacity
    return (A << 24) | (B << 16) | (G << 8) | (R << 0);
}

// -----------------------------------------------------------------------------
// BuildQuad utilities for each face direction
// -----------------------------------------------------------------------------
static void buildQuadPosZ(
    int startX, int startY, int width, int height, int z,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float Zpos = float(z + 1 + offsetZ);
    float X0 = float(startX + offsetX);
    float Y0 = float(startY + offsetY);
    float X1 = float(startX + width + offsetX);
    float Y1 = float(startY + height + offsetY);

    int baseIdx = (int)outVertices.size();
    outVertices.emplace_back(X0, Y0, Zpos, col);
    outVertices.emplace_back(X1, Y0, Zpos, col);
    outVertices.emplace_back(X1, Y1, Zpos, col);
    outVertices.emplace_back(X0, Y1, Zpos, col);

    outIndices.push_back(baseIdx + 0);
    outIndices.push_back(baseIdx + 1);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 3);
    outIndices.push_back(baseIdx + 0);
}

static void buildQuadNegZ(
    int startX, int startY, int width, int height, int z,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float Zpos = float(z + offsetZ);
    float X0 = float(startX + offsetX);
    float Y0 = float(startY + offsetY);
    float X1 = float(startX + width + offsetX);
    float Y1 = float(startY + height + offsetY);

    int baseIdx = (int)outVertices.size();
    // reversed winding
    outVertices.emplace_back(X1, Y0, Zpos, col);
    outVertices.emplace_back(X0, Y0, Zpos, col);
    outVertices.emplace_back(X0, Y1, Zpos, col);
    outVertices.emplace_back(X1, Y1, Zpos, col);

    outIndices.push_back(baseIdx + 0);
    outIndices.push_back(baseIdx + 1);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 3);
    outIndices.push_back(baseIdx + 0);
}

static void buildQuadPosX(
    int startY, int startZ, int height, int depth, int x,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float Xpos = float(x + 1 + offsetX);
    float Y0 = float(startY + offsetY);
    float Z0 = float(startZ + offsetZ);
    float Y1 = float(startY + height + offsetY);
    float Z1 = float(startZ + depth + offsetZ);

    int baseIdx = (int)outVertices.size();
    outVertices.emplace_back(Xpos, Y0, Z0, col);
    outVertices.emplace_back(Xpos, Y0, Z1, col);
    outVertices.emplace_back(Xpos, Y1, Z1, col);
    outVertices.emplace_back(Xpos, Y1, Z0, col);

    outIndices.push_back(baseIdx + 0);
    outIndices.push_back(baseIdx + 1);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 3);
    outIndices.push_back(baseIdx + 0);
}

static void buildQuadNegX(
    int startY, int startZ, int height, int depth, int x,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float Xpos = float(x + offsetX);
    float Y0 = float(startY + offsetY);
    float Z0 = float(startZ + offsetZ);
    float Y1 = float(startY + height + offsetY);
    float Z1 = float(startZ + depth + offsetZ);

    int baseIdx = (int)outVertices.size();
    // reversed winding for -X
    outVertices.emplace_back(Xpos, Y0, Z1, col);
    outVertices.emplace_back(Xpos, Y0, Z0, col);
    outVertices.emplace_back(Xpos, Y1, Z0, col);
    outVertices.emplace_back(Xpos, Y1, Z1, col);

    outIndices.push_back(baseIdx + 0);
    outIndices.push_back(baseIdx + 1);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 3);
    outIndices.push_back(baseIdx + 0);
}

static void buildQuadPosY(
    int startX, int startZ, int width, int depth, int y,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float Ypos = float(y + 1 + offsetY);
    float X0 = float(startX + offsetX);
    float Z0 = float(startZ + offsetZ);
    float X1 = float(startX + width + offsetX);
    float Z1 = float(startZ + depth + offsetZ);

    int baseIdx = (int)outVertices.size();
    outVertices.emplace_back(X0, Ypos, Z0, col);
    outVertices.emplace_back(X1, Ypos, Z0, col);
    outVertices.emplace_back(X1, Ypos, Z1, col);
    outVertices.emplace_back(X0, Ypos, Z1, col);

    outIndices.push_back(baseIdx + 0);
    outIndices.push_back(baseIdx + 1);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 3);
    outIndices.push_back(baseIdx + 0);
}

static void buildQuadNegY(
    int startX, int startZ, int width, int depth, int y,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float Ypos = float(y + offsetY);
    float X0 = float(startX + offsetX);
    float Z0 = float(startZ + offsetZ);
    float X1 = float(startX + width + offsetX);
    float Z1 = float(startZ + depth + offsetZ);

    int baseIdx = (int)outVertices.size();
    // reversed winding for -Y
    outVertices.emplace_back(X1, Ypos, Z0, col);
    outVertices.emplace_back(X0, Ypos, Z0, col);
    outVertices.emplace_back(X0, Ypos, Z1, col);
    outVertices.emplace_back(X1, Ypos, Z1, col);

    outIndices.push_back(baseIdx + 0);
    outIndices.push_back(baseIdx + 1);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 2);
    outIndices.push_back(baseIdx + 3);
    outIndices.push_back(baseIdx + 0);
}

// -----------------------------------------------------------------------------
// GreedyMesher Implementation
// -----------------------------------------------------------------------------

bool GreedyMesher::generateMesh(
    Chunk& chunk,
    int cx, int cy, int cz,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    int offsetX, int offsetY, int offsetZ,
    const ChunkManager& manager
) const
{
    // 1) Because Chunk implements IBlockProvider, we just cast and call 
    //    the new version that works for any IBlockProvider.
    return generateMesh(
        static_cast<const IBlockProvider&>(chunk),
        cx, cy, cz,
        outVertices, outIndices,
        offsetX, offsetY, offsetZ,
        manager
    );
}

bool GreedyMesher::generateMesh(
    const IBlockProvider& blockData,
    int cx, int cy, int cz,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    int offsetX, int offsetY, int offsetZ,
    const ChunkManager& manager
) const
{
    // 2) Clear outputs
    outVertices.clear();
    outIndices.clear();

    // 3) Reserve some capacity
    outVertices.reserve(4096);
    outIndices.reserve(6144);

    // 4) Basic dimensions
    const int sizeX = blockData.getSizeX();
    const int sizeY = blockData.getSizeY();
    const int sizeZ = blockData.getSizeZ();

    // Provide a quick "isSolidID" by checking VoxelType
    auto isSolidID = [&](int voxelID) -> bool {
        if (voxelID < 0) return false; // treat negative => air
        const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
        return vt.isSolid;
        };

    // local accessor
    auto getLocalVoxel = [&](int x, int y, int z) -> int {
        return blockData.getBlock(x, y, z);
        };

    // For neighbor logic, we can do manager lookups if we want, 
    // or treat out-of-range as empty for LOD. We'll do a minimal approach:
    auto isSolidGlobal = [&](int x, int y, int z) -> bool
        {
            if (x >= 0 && x < sizeX &&
                y >= 0 && y < sizeY &&
                z >= 0 && z < sizeZ)
            {
                int id = getLocalVoxel(x, y, z);
                return (id > 0) ? isSolidID(id) : false;
            }
            else {
                // If you want chunk neighbors, you'd do manager.getChunk(...) etc. 
                // For LOD or mini-chunk, we’ll treat out-of-range as empty.
                return false;
            }
        };

    // =========== +Z Faces ===========
    for (int z = 0; z < sizeZ; z++) {
        std::vector<int> mask(sizeX * sizeY, -1);
        for (int y = 0; y < sizeY; y++) {
            for (int x = 0; x < sizeX; x++) {
                int id = getLocalVoxel(x, y, z);
                if (id <= 0) continue;
                bool exposed = !isSolidGlobal(x, y, z + 1);
                size_t idx = (size_t)y * sizeX + x;
                mask[idx] = exposed ? id : -1;
            }
        }
        for (int row = 0; row < sizeY; row++) {
            int col = 0;
            while (col < sizeX) {
                size_t m = (size_t)row * sizeX + col;
                int currentID = mask[m];
                if (currentID < 0) {
                    col++;
                    continue;
                }
                // find width
                int width = 1;
                while (col + width < sizeX) {
                    size_t m2 = (size_t)row * sizeX + (col + width);
                    if (mask[m2] == currentID) width++;
                    else break;
                }
                // find height
                int height = 1;
                bool done = false;
                while (!done && (row + height) < sizeY) {
                    for (int k = 0; k < width; k++) {
                        size_t m3 = (size_t)(row + height) * sizeX + (col + k);
                        if (mask[m3] != currentID) {
                            done = true;
                            break;
                        }
                    }
                    if (!done) height++;
                }
                // build quad
                buildQuadPosZ(
                    col, row, width, height, z,
                    offsetX, offsetY, offsetZ,
                    currentID,
                    outVertices, outIndices
                );
                // clear
                for (int dy = 0; dy < height; dy++) {
                    for (int dx = 0; dx < width; dx++) {
                        size_t m4 = (size_t)(row + dy) * sizeX + (col + dx);
                        mask[m4] = -1;
                    }
                }
                col += width;
            }
        }
    }

    // =========== -Z Faces ===========
    {
        for (int z = 0; z < sizeZ; z++) {
            std::vector<int> mask(sizeX * sizeY, -1);
            for (int y = 0; y < sizeY; y++) {
                for (int x = 0; x < sizeX; x++) {
                    int id = getLocalVoxel(x, y, z);
                    if (id <= 0) continue;
                    bool exposed = !isSolidGlobal(x, y, z - 1);
                    size_t idx = (size_t)y * sizeX + x;
                    mask[idx] = exposed ? id : -1;
                }
            }
            for (int row = 0; row < sizeY; row++) {
                int col = 0;
                while (col < sizeX) {
                    size_t m = (size_t)row * sizeX + col;
                    int currentID = mask[m];
                    if (currentID < 0) {
                        col++;
                        continue;
                    }
                    // find width
                    int width = 1;
                    while (col + width < sizeX) {
                        size_t m2 = (size_t)row * sizeX + (col + width);
                        if (mask[m2] == currentID) width++;
                        else break;
                    }
                    // find height
                    int height = 1;
                    bool done = false;
                    while (!done && (row + height) < sizeY) {
                        for (int k = 0; k < width; k++) {
                            size_t m3 = (size_t)(row + height) * sizeX + (col + k);
                            if (mask[m3] != currentID) {
                                done = true;
                                break;
                            }
                        }
                        if (!done) height++;
                    }
                    buildQuadNegZ(
                        col, row, width, height, z,
                        offsetX, offsetY, offsetZ,
                        currentID,
                        outVertices, outIndices
                    );
                    for (int dy = 0; dy < height; dy++) {
                        for (int dx = 0; dx < width; dx++) {
                            size_t m4 = (size_t)(row + dy) * sizeX + (col + dx);
                            mask[m4] = -1;
                        }
                    }
                    col += width;
                }
            }
        }
    }

    // =========== +X Faces ===========
    {
        for (int x = 0; x < sizeX; x++) {
            std::vector<int> mask(sizeY * sizeZ, -1);
            for (int z = 0; z < sizeZ; z++) {
                for (int y = 0; y < sizeY; y++) {
                    int id = getLocalVoxel(x, y, z);
                    if (id <= 0) continue;
                    bool exposed = !isSolidGlobal(x + 1, y, z);
                    size_t idx = (size_t)z * sizeY + y;
                    mask[idx] = exposed ? id : -1;
                }
            }
            for (int row = 0; row < sizeZ; row++) {
                int col = 0;
                while (col < sizeY) {
                    size_t m = (size_t)row * sizeY + col;
                    int currentID = mask[m];
                    if (currentID < 0) {
                        col++;
                        continue;
                    }
                    int height = 1;
                    while (col + height < sizeY) {
                        size_t m2 = (size_t)row * sizeY + (col + height);
                        if (mask[m2] == currentID) height++;
                        else break;
                    }
                    int depth = 1;
                    bool done = false;
                    while (!done && row + depth < sizeZ) {
                        for (int k = 0; k < height; k++) {
                            size_t m3 = (size_t)(row + depth) * sizeY + (col + k);
                            if (mask[m3] != currentID) {
                                done = true;
                                break;
                            }
                        }
                        if (!done) depth++;
                    }
                    buildQuadPosX(
                        col, row, height, depth, x,
                        offsetX, offsetY, offsetZ,
                        currentID,
                        outVertices, outIndices
                    );
                    for (int d = 0; d < depth; d++) {
                        for (int h = 0; h < height; h++) {
                            size_t m4 = (size_t)(row + d) * sizeY + (col + h);
                            mask[m4] = -1;
                        }
                    }
                    col += height;
                }
            }
        }
    }

    // =========== -X Faces ===========
    {
        for (int x = 0; x < sizeX; x++) {
            std::vector<int> mask(sizeY * sizeZ, -1);
            for (int z = 0; z < sizeZ; z++) {
                for (int y = 0; y < sizeY; y++) {
                    int id = getLocalVoxel(x, y, z);
                    if (id <= 0) continue;
                    bool exposed = !isSolidGlobal(x - 1, y, z);
                    size_t idx = (size_t)z * sizeY + y;
                    mask[idx] = exposed ? id : -1;
                }
            }
            for (int row = 0; row < sizeZ; row++) {
                int col = 0;
                while (col < sizeY) {
                    size_t m = (size_t)row * sizeY + col;
                    int currentID = mask[m];
                    if (currentID < 0) {
                        col++;
                        continue;
                    }
                    int height = 1;
                    while (col + height < sizeY) {
                        size_t m2 = (size_t)row * sizeY + (col + height);
                        if (mask[m2] == currentID) height++;
                        else break;
                    }
                    int depth = 1;
                    bool done = false;
                    while (!done && row + depth < sizeZ) {
                        for (int k = 0; k < height; k++) {
                            size_t m3 = (size_t)(row + depth) * sizeY + (col + k);
                            if (mask[m3] != currentID) {
                                done = true;
                                break;
                            }
                        }
                        if (!done) depth++;
                    }
                    buildQuadNegX(
                        col, row, height, depth, x,
                        offsetX, offsetY, offsetZ,
                        currentID,
                        outVertices, outIndices
                    );
                    for (int d = 0; d < depth; d++) {
                        for (int h = 0; h < height; h++) {
                            size_t m4 = (size_t)(row + d) * sizeY + (col + h);
                            mask[m4] = -1;
                        }
                    }
                    col += height;
                }
            }
        }
    }

    // =========== +Y Faces ===========
    {
        for (int y = 0; y < sizeY; y++) {
            std::vector<int> mask(sizeX * sizeZ, -1);
            for (int z = 0; z < sizeZ; z++) {
                for (int x = 0; x < sizeX; x++) {
                    int id = getLocalVoxel(x, y, z);
                    if (id <= 0) continue;
                    bool exposed = !isSolidGlobal(x, y + 1, z);
                    size_t idx = (size_t)z * sizeX + x;
                    mask[idx] = exposed ? id : -1;
                }
            }
            for (int row = 0; row < sizeZ; row++) {
                int col = 0;
                while (col < sizeX) {
                    size_t m = (size_t)row * sizeX + col;
                    int currentID = mask[m];
                    if (currentID < 0) {
                        col++;
                        continue;
                    }
                    int width = 1;
                    while (col + width < sizeX) {
                        size_t m2 = (size_t)row * sizeX + (col + width);
                        if (mask[m2] == currentID) width++;
                        else break;
                    }
                    int depth = 1;
                    bool done = false;
                    while (!done && (row + depth) < sizeZ) {
                        for (int k = 0; k < width; k++) {
                            size_t m3 = (size_t)(row + depth) * sizeX + (col + k);
                            if (mask[m3] != currentID) {
                                done = true;
                                break;
                            }
                        }
                        if (!done) depth++;
                    }
                    buildQuadPosY(
                        col, row, width, depth, y,
                        offsetX, offsetY, offsetZ,
                        currentID,
                        outVertices, outIndices
                    );
                    for (int d = 0; d < depth; d++) {
                        for (int w = 0; w < width; w++) {
                            size_t m4 = (size_t)(row + d) * sizeX + (col + w);
                            mask[m4] = -1;
                        }
                    }
                    col += width;
                }
            }
        }
    }

    // =========== -Y Faces ===========
    {
        for (int y = 0; y < sizeY; y++) {
            std::vector<int> mask(sizeX * sizeZ, -1);
            for (int z = 0; z < sizeZ; z++) {
                for (int x = 0; x < sizeX; x++) {
                    int id = getLocalVoxel(x, y, z);
                    if (id <= 0) continue;
                    bool exposed = !isSolidGlobal(x, y - 1, z);
                    size_t idx = (size_t)z * sizeX + x;
                    mask[idx] = exposed ? id : -1;
                }
            }
            for (int row = 0; row < sizeZ; row++) {
                int col = 0;
                while (col < sizeX) {
                    size_t m = (size_t)row * sizeX + col;
                    int currentID = mask[m];
                    if (currentID < 0) {
                        col++;
                        continue;
                    }
                    int width = 1;
                    while (col + width < sizeX) {
                        size_t m2 = (size_t)row * sizeX + (col + width);
                        if (mask[m2] == currentID) width++;
                        else break;
                    }
                    int depth = 1;
                    bool done = false;
                    while (!done && (row + depth) < sizeZ) {
                        for (int k = 0; k < width; k++) {
                            size_t m3 = (size_t)(row + depth) * sizeX + (col + k);
                            if (mask[m3] != currentID) {
                                done = true;
                                break;
                            }
                        }
                        if (!done) depth++;
                    }
                    buildQuadNegY(
                        col, row, width, depth, y,
                        offsetX, offsetY, offsetZ,
                        currentID,
                        outVertices, outIndices
                    );
                    for (int d = 0; d < depth; d++) {
                        for (int w = 0; w < width; w++) {
                            size_t m4 = (size_t)(row + d) * sizeX + (col + w);
                            mask[m4] = -1;
                        }
                    }
                    col += width;
                }
            }
        }
    }

    // Return whether we generated geometry
    return !outVertices.empty();
}
