#include "GreedyMesher.h"
#include "../Chunk.h"
#include "../ChunkManager.h"
#include "IMesher.h"
#include "../VoxelTypeRegistry.h"
#include "../VoxelType.h"
#include <vector>
#include <cstddef>
#include <stdexcept>
#include <algorithm>

// -----------------------------------------------------------------------------
// Helper to pack float color [0..1] into a 32-bit RGBA8 format.
// -----------------------------------------------------------------------------
static uint32_t packColor(float r, float g, float b)
{
    // Clamp each channel
    if (r < 0.f) r = 0.f; if (r > 1.f) r = 1.f;
    if (g < 0.f) g = 0.f; if (g > 1.f) g = 1.f;
    if (b < 0.f) b = 0.f; if (b > 1.f) b = 1.f;

    uint32_t R = static_cast<uint32_t>(r * 255.0f);
    uint32_t G = static_cast<uint32_t>(g * 255.0f);
    uint32_t B = static_cast<uint32_t>(b * 255.0f);
    uint32_t A = 255; // full opacity

    // Typically RGBA is in the order: A << 24 | B << 16 | G << 8 | R.
    // But you can reorder if you prefer BGRA or something else in the shader.
    // We'll store it as 0xAABBGGRR (little-endian machine reads it as RGBA).
    return (A << 24) | (B << 16) | (G << 8) | (R << 0);
}

//-------------------- Utility Functions for Building Quads --------------------

// +Z face
static void buildQuadPosZ(
    int startX, int startY, int width, int height, int z,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    // Inline getVoxel => color
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    uint32_t packedColor = packColor(r, g, b);

    float zPos = static_cast<float>(z + 1 + offsetZ);
    float X0 = static_cast<float>(startX + offsetX);
    float Y0 = static_cast<float>(startY + offsetY);
    float X1 = static_cast<float>(startX + width + offsetX);
    float Y1 = static_cast<float>(startY + height + offsetY);

    int startIndex = static_cast<int>(outVertices.size());
    outVertices.push_back(Vertex(X0, Y0, zPos, packedColor));
    outVertices.push_back(Vertex(X1, Y0, zPos, packedColor));
    outVertices.push_back(Vertex(X1, Y1, zPos, packedColor));
    outVertices.push_back(Vertex(X0, Y1, zPos, packedColor));

    // Indices
    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

// -Z face
static void buildQuadNegZ(
    int startX, int startY, int width, int height, int z,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    uint32_t packedColor = packColor(r, g, b);

    float zPos = static_cast<float>(z + offsetZ);
    float X0 = static_cast<float>(startX + offsetX);
    float Y0 = static_cast<float>(startY + offsetY);
    float X1 = static_cast<float>(startX + width + offsetX);
    float Y1 = static_cast<float>(startY + height + offsetY);

    int startIndex = static_cast<int>(outVertices.size());
    // Reverse winding for -Z
    outVertices.push_back(Vertex(X1, Y0, zPos, packedColor));
    outVertices.push_back(Vertex(X0, Y0, zPos, packedColor));
    outVertices.push_back(Vertex(X0, Y1, zPos, packedColor));
    outVertices.push_back(Vertex(X1, Y1, zPos, packedColor));

    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

// +X face
static void buildQuadPosX(
    int startY, int startZ, int height, int depth, int x,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    uint32_t packedColor = packColor(r, g, b);

    float xPos = static_cast<float>(x + 1 + offsetX);
    float Y0 = static_cast<float>(startY + offsetY);
    float Z0 = static_cast<float>(startZ + offsetZ);
    float Y1 = static_cast<float>(startY + height + offsetY);
    float Z1 = static_cast<float>(startZ + depth + offsetZ);

    int startIndex = static_cast<int>(outVertices.size());
    outVertices.push_back(Vertex(xPos, Y0, Z0, packedColor));
    outVertices.push_back(Vertex(xPos, Y0, Z1, packedColor));
    outVertices.push_back(Vertex(xPos, Y1, Z1, packedColor));
    outVertices.push_back(Vertex(xPos, Y1, Z0, packedColor));

    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

// -X face
static void buildQuadNegX(
    int startY, int startZ, int height, int depth, int x,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    uint32_t packedColor = packColor(r, g, b);

    float xPos = static_cast<float>(x + offsetX);
    float Y0 = static_cast<float>(startY + offsetY);
    float Z0 = static_cast<float>(startZ + offsetZ);
    float Y1 = static_cast<float>(startY + height + offsetY);
    float Z1 = static_cast<float>(startZ + depth + offsetZ);

    int startIndex = static_cast<int>(outVertices.size());
    // Reverse winding for -X
    outVertices.push_back(Vertex(xPos, Y0, Z1, packedColor));
    outVertices.push_back(Vertex(xPos, Y0, Z0, packedColor));
    outVertices.push_back(Vertex(xPos, Y1, Z0, packedColor));
    outVertices.push_back(Vertex(xPos, Y1, Z1, packedColor));

    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

// +Y face
static void buildQuadPosY(
    int startX, int startZ, int width, int depth, int y,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    uint32_t packedColor = packColor(r, g, b);

    float yPos = static_cast<float>(y + 1 + offsetY);
    float X0 = static_cast<float>(startX + offsetX);
    float Z0 = static_cast<float>(startZ + offsetZ);
    float X1 = static_cast<float>(startX + width + offsetX);
    float Z1 = static_cast<float>(startZ + depth + offsetZ);

    int startIndex = static_cast<int>(outVertices.size());
    outVertices.push_back(Vertex(X0, yPos, Z0, packedColor));
    outVertices.push_back(Vertex(X1, yPos, Z0, packedColor));
    outVertices.push_back(Vertex(X1, yPos, Z1, packedColor));
    outVertices.push_back(Vertex(X0, yPos, Z1, packedColor));

    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

// -Y face
static void buildQuadNegY(
    int startX, int startZ, int width, int depth, int y,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    uint32_t packedColor = packColor(r, g, b);

    float yPos = static_cast<float>(y + offsetY);
    float X0 = static_cast<float>(startX + offsetX);
    float Z0 = static_cast<float>(startZ + offsetZ);
    float X1 = static_cast<float>(startX + width + offsetX);
    float Z1 = static_cast<float>(startZ + depth + offsetZ);

    int startIndex = static_cast<int>(outVertices.size());
    // Reverse winding for -Y
    outVertices.push_back(Vertex(X1, yPos, Z0, packedColor));
    outVertices.push_back(Vertex(X0, yPos, Z0, packedColor));
    outVertices.push_back(Vertex(X0, yPos, Z1, packedColor));
    outVertices.push_back(Vertex(X1, yPos, Z1, packedColor));

    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

//-------------------- Implementation of generateMesh --------------------

bool GreedyMesher::generateMesh(
    Chunk& chunk,
    int cx, int cy, int cz,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    int offsetX, int offsetY, int offsetZ,
    const ChunkManager& manager) const
{
    // 1) Clear existing geometry
    outVertices.clear();
    outIndices.clear();

    // 2) Reserve some capacity to reduce dynamic allocations
    outVertices.reserve(4096);
    outIndices.reserve(6144);

    // 3) Cache chunk's voxel data in a local array
    const int sizeX = Chunk::SIZE_X;
    const int sizeY = Chunk::SIZE_Y;
    const int sizeZ = Chunk::SIZE_Z;
    std::vector<int> localVoxels;
    localVoxels.resize(sizeX * sizeY * sizeZ, 0);

    for (int z = 0; z < sizeZ; z++) {
        for (int y = 0; y < sizeY; y++) {
            for (int x = 0; x < sizeX; x++) {
                localVoxels[z * sizeY * sizeX + y * sizeX + x] = chunk.getBlock(x, y, z);
            }
        }
    }

    // isSolidID checks if a block ID is a solid voxel
    auto isSolidID = [](int voxelID) -> bool {
        if (voxelID < 0) return false;
        const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
        return vt.isSolid;
        };

    // Quick local accessor
    auto getLocalVoxel = [&](int x, int y, int z) -> int {
        return localVoxels[z * sizeY * sizeX + y * sizeX + x];
        };

    // For out-of-bounds => check neighbor chunk
    auto isSolidGlobal = [&](int x, int y, int z) -> bool {
        if (x >= 0 && x < sizeX && y >= 0 && y < sizeY && z >= 0 && z < sizeZ)
        {
            int nid = getLocalVoxel(x, y, z);
            return (nid > 0) ? isSolidID(nid) : false;
        }
        else {
            int nx = cx, ny = cy, nz = cz;
            int localX = x, localY = y, localZ = z;

            if (x < 0) { nx -= 1; localX += sizeX; }
            else if (x >= sizeX) { nx += 1; localX -= sizeX; }

            if (y < 0) { ny -= 1; localY += sizeY; }
            else if (y >= sizeY) { ny += 1; localY -= sizeY; }

            if (z < 0) { nz -= 1; localZ += sizeZ; }
            else if (z >= sizeZ) { nz += 1; localZ -= sizeZ; }

            const Chunk* neighbor = manager.getChunk(nx, ny, nz);
            if (!neighbor) return false;
            int nid = neighbor->getBlock(localX, localY, localZ);
            return (nid > 0) ? isSolidID(nid) : false;
        }
        };

    // ---------- +Z Faces ----------
    for (int z = 0; z < sizeZ; z++) {
        std::vector<int> mask(sizeX * sizeY, -1);
        for (int y = 0; y < sizeY; y++) {
            for (int x = 0; x < sizeX; x++) {
                int id = getLocalVoxel(x, y, z);
                if (id <= 0) continue;
                bool exposed = !isSolidGlobal(x, y, z + 1);
                size_t idx = static_cast<size_t>(y) * sizeX + x;
                mask[idx] = exposed ? id : -1;
            }
        }
        for (int row = 0; row < sizeY; row++) {
            int col = 0;
            while (col < sizeX) {
                size_t idx = static_cast<size_t>(row) * sizeX + col;
                int currentID = mask[idx];
                if (currentID < 0) { col++; continue; }
                int width = 1;
                while (col + width < sizeX) {
                    size_t idx2 = static_cast<size_t>(row) * sizeX + (col + width);
                    if (mask[idx2] == currentID) width++;
                    else break;
                }
                int height = 1;
                bool done = false;
                while (!done && row + height < sizeY) {
                    for (int k = 0; k < width; k++) {
                        size_t idx3 = static_cast<size_t>(row + height) * sizeX + (col + k);
                        if (mask[idx3] != currentID) { done = true; break; }
                    }
                    if (!done) height++;
                }
                buildQuadPosZ(col, row, width, height, z,
                    offsetX, offsetY, offsetZ,
                    currentID,
                    outVertices, outIndices);
                for (int dy = 0; dy < height; dy++) {
                    for (int dx = 0; dx < width; dx++) {
                        size_t idx4 = static_cast<size_t>(row + dy) * sizeX + (col + dx);
                        mask[idx4] = -1;
                    }
                }
                col += width;
            }
        }
    }

    // ---------- -Z Faces ----------
    for (int z = 0; z < sizeZ; z++) {
        std::vector<int> mask(sizeX * sizeY, -1);
        for (int y = 0; y < sizeY; y++) {
            for (int x = 0; x < sizeX; x++) {
                int id = getLocalVoxel(x, y, z);
                if (id <= 0) continue;
                bool exposed = !isSolidGlobal(x, y, z - 1);
                size_t idx = static_cast<size_t>(y) * sizeX + x;
                mask[idx] = exposed ? id : -1;
            }
        }
        for (int row = 0; row < sizeY; row++) {
            int col = 0;
            while (col < sizeX) {
                size_t idx = static_cast<size_t>(row) * sizeX + col;
                int currentID = mask[idx];
                if (currentID < 0) { col++; continue; }
                int width = 1;
                while (col + width < sizeX) {
                    size_t idx2 = static_cast<size_t>(row) * sizeX + (col + width);
                    if (mask[idx2] == currentID) width++;
                    else break;
                }
                int height = 1;
                bool done = false;
                while (!done && row + height < sizeY) {
                    for (int k = 0; k < width; k++) {
                        size_t idx3 = static_cast<size_t>(row + height) * sizeX + (col + k);
                        if (mask[idx3] != currentID) { done = true; break; }
                    }
                    if (!done) height++;
                }
                buildQuadNegZ(col, row, width, height, z,
                    offsetX, offsetY, offsetZ,
                    currentID,
                    outVertices, outIndices);
                for (int dy = 0; dy < height; dy++) {
                    for (int dx = 0; dx < width; dx++) {
                        size_t idx4 = static_cast<size_t>(row + dy) * sizeX + (col + dx);
                        mask[idx4] = -1;
                    }
                }
                col += width;
            }
        }
    }

    // ---------- +X Faces ----------
    for (int x = 0; x < sizeX; x++) {
        std::vector<int> mask(sizeY * sizeZ, -1);
        for (int z = 0; z < sizeZ; z++) {
            for (int y = 0; y < sizeY; y++) {
                int id = getLocalVoxel(x, y, z);
                if (id <= 0) continue;
                bool exposed = !isSolidGlobal(x + 1, y, z);
                size_t idx = static_cast<size_t>(z) * sizeY + y;
                mask[idx] = exposed ? id : -1;
            }
        }
        for (int row = 0; row < sizeZ; row++) {
            int col = 0;
            while (col < sizeY) {
                size_t idx = static_cast<size_t>(row) * sizeY + col;
                int currentID = mask[idx];
                if (currentID < 0) { col++; continue; }
                int height = 1;
                while (col + height < sizeY) {
                    size_t idx2 = static_cast<size_t>(row) * sizeY + (col + height);
                    if (mask[idx2] == currentID) height++;
                    else break;
                }
                int depth = 1;
                bool done = false;
                while (!done && row + depth < sizeZ) {
                    for (int k = 0; k < height; k++) {
                        size_t idx3 = static_cast<size_t>(row + depth) * sizeY + (col + k);
                        if (mask[idx3] != currentID) { done = true; break; }
                    }
                    if (!done) depth++;
                }
                buildQuadPosX(col, row, height, depth, x,
                    offsetX, offsetY, offsetZ,
                    currentID,
                    outVertices, outIndices);
                for (int d = 0; d < depth; d++) {
                    for (int h = 0; h < height; h++) {
                        size_t idx4 = static_cast<size_t>(row + d) * sizeY + (col + h);
                        mask[idx4] = -1;
                    }
                }
                col += height;
            }
        }
    }

    // ---------- -X Faces ----------
    for (int x = 0; x < sizeX; x++) {
        std::vector<int> mask(sizeY * sizeZ, -1);
        for (int z = 0; z < sizeZ; z++) {
            for (int y = 0; y < sizeY; y++) {
                int id = getLocalVoxel(x, y, z);
                if (id <= 0) continue;
                bool exposed = !isSolidGlobal(x - 1, y, z);
                size_t idx = static_cast<size_t>(z) * sizeY + y;
                mask[idx] = exposed ? id : -1;
            }
        }
        for (int row = 0; row < sizeZ; row++) {
            int col = 0;
            while (col < sizeY) {
                size_t idx = static_cast<size_t>(row) * sizeY + col;
                int currentID = mask[idx];
                if (currentID < 0) { col++; continue; }
                int height = 1;
                while (col + height < sizeY) {
                    size_t idx2 = static_cast<size_t>(row) * sizeY + (col + height);
                    if (mask[idx2] == currentID) height++;
                    else break;
                }
                int depth = 1;
                bool done = false;
                while (!done && row + depth < sizeZ) {
                    for (int k = 0; k < height; k++) {
                        size_t idx3 = static_cast<size_t>(row + depth) * sizeY + (col + k);
                        if (mask[idx3] != currentID) { done = true; break; }
                    }
                    if (!done) depth++;
                }
                buildQuadNegX(col, row, height, depth, x,
                    offsetX, offsetY, offsetZ,
                    currentID,
                    outVertices, outIndices);
                for (int d = 0; d < depth; d++) {
                    for (int h = 0; h < height; h++) {
                        size_t idx4 = static_cast<size_t>(row + d) * sizeY + (col + h);
                        mask[idx4] = -1;
                    }
                }
                col += height;
            }
        }
    }

    // ---------- +Y Faces ----------
    for (int y = 0; y < sizeY; y++) {
        std::vector<int> mask(sizeX * sizeZ, -1);
        for (int z = 0; z < sizeZ; z++) {
            for (int x = 0; x < sizeX; x++) {
                int id = getLocalVoxel(x, y, z);
                if (id <= 0) continue;
                bool exposed = !isSolidGlobal(x, y + 1, z);
                size_t idx = static_cast<size_t>(z) * sizeX + x;
                mask[idx] = exposed ? id : -1;
            }
        }
        for (int row = 0; row < sizeZ; row++) {
            int col = 0;
            while (col < sizeX) {
                size_t idx = static_cast<size_t>(row) * sizeX + col;
                int currentID = mask[idx];
                if (currentID < 0) { col++; continue; }
                int width = 1;
                while (col + width < sizeX) {
                    size_t idx2 = static_cast<size_t>(row) * sizeX + (col + width);
                    if (mask[idx2] == currentID) width++;
                    else break;
                }
                int depth = 1;
                bool done = false;
                while (!done && row + depth < sizeZ) {
                    for (int k = 0; k < width; k++) {
                        size_t idx3 = static_cast<size_t>(row + depth) * sizeX + (col + k);
                        if (mask[idx3] != currentID) { done = true; break; }
                    }
                    if (!done) depth++;
                }
                buildQuadPosY(col, row, width, depth, y,
                    offsetX, offsetY, offsetZ,
                    currentID,
                    outVertices, outIndices);
                for (int d = 0; d < depth; d++) {
                    for (int w = 0; w < width; w++) {
                        size_t idx4 = static_cast<size_t>(row + d) * sizeX + (col + w);
                        mask[idx4] = -1;
                    }
                }
                col += width;
            }
        }
    }

    // ---------- -Y Faces ----------
    for (int y = 0; y < sizeY; y++) {
        std::vector<int> mask(sizeX * sizeZ, -1);
        for (int z = 0; z < sizeZ; z++) {
            for (int x = 0; x < sizeX; x++) {
                int id = getLocalVoxel(x, y, z);
                if (id <= 0) continue;
                bool exposed = !isSolidGlobal(x, y - 1, z);
                size_t idx = static_cast<size_t>(z) * sizeX + x;
                mask[idx] = exposed ? id : -1;
            }
        }
        for (int row = 0; row < sizeZ; row++) {
            int col = 0;
            while (col < sizeX) {
                size_t idx = static_cast<size_t>(row) * sizeX + col;
                int currentID = mask[idx];
                if (currentID < 0) { col++; continue; }
                int width = 1;
                while (col + width < sizeX) {
                    size_t idx2 = static_cast<size_t>(row) * sizeX + (col + width);
                    if (mask[idx2] == currentID) width++;
                    else break;
                }
                int depth = 1;
                bool done = false;
                while (!done && row + depth < sizeZ) {
                    for (int k = 0; k < width; k++) {
                        size_t idx3 = static_cast<size_t>(row + depth) * sizeX + (col + k);
                        if (mask[idx3] != currentID) { done = true; break; }
                    }
                    if (!done) depth++;
                }
                buildQuadNegY(col, row, width, depth, y,
                    offsetX, offsetY, offsetZ,
                    currentID,
                    outVertices, outIndices);
                for (int d = 0; d < depth; d++) {
                    for (int w = 0; w < width; w++) {
                        size_t idx4 = static_cast<size_t>(row + d) * sizeX + (col + w);
                        mask[idx4] = -1;
                    }
                }
                col += width;
            }
        }
    }

    // Return whether we generated any geometry
    return !outVertices.empty();
}
