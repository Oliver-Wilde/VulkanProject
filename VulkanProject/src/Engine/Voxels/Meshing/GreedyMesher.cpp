#include "GreedyMesher.h"
#include "../Chunk.h"
#include "../ChunkManager.h"
#include "IMesher.h"  // Assumed to define Vertex
#include "../VoxelTypeRegistry.h"
#include "../VoxelType.h"
#include <vector>
#include <cstddef>
#include <stdexcept>
#include <algorithm>

//-------------------- Utility functions for building quads --------------------

// +Z face (front)
static void buildQuadPosZ(
    int startX, int startY, int width, int height, int z,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    float zPos = static_cast<float>(z + 1 + offsetZ);
    float X0 = static_cast<float>(startX + offsetX);
    float Y0 = static_cast<float>(startY + offsetY);
    float X1 = static_cast<float>(startX + width + offsetX);
    float Y1 = static_cast<float>(startY + height + offsetY);
    int startIndex = static_cast<int>(outVertices.size());
    outVertices.push_back(Vertex(X0, Y0, zPos, r, g, b));
    outVertices.push_back(Vertex(X1, Y0, zPos, r, g, b));
    outVertices.push_back(Vertex(X1, Y1, zPos, r, g, b));
    outVertices.push_back(Vertex(X0, Y1, zPos, r, g, b));
    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

// -Z face (back)
static void buildQuadNegZ(
    int startX, int startY, int width, int height, int z,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    float zPos = static_cast<float>(z + offsetZ);
    float X0 = static_cast<float>(startX + offsetX);
    float Y0 = static_cast<float>(startY + offsetY);
    float X1 = static_cast<float>(startX + width + offsetX);
    float Y1 = static_cast<float>(startY + height + offsetY);
    int startIndex = static_cast<int>(outVertices.size());
    // Reverse winding order for -Z face
    outVertices.push_back(Vertex(X1, Y0, zPos, r, g, b));
    outVertices.push_back(Vertex(X0, Y0, zPos, r, g, b));
    outVertices.push_back(Vertex(X0, Y1, zPos, r, g, b));
    outVertices.push_back(Vertex(X1, Y1, zPos, r, g, b));
    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

// +X face (right)
static void buildQuadPosX(
    int startY, int startZ, int height, int depth, int x,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    float xPos = static_cast<float>(x + 1 + offsetX);
    float Y0 = static_cast<float>(startY + offsetY);
    float Z0 = static_cast<float>(startZ + offsetZ);
    float Y1 = static_cast<float>(startY + height + offsetY);
    float Z1 = static_cast<float>(startZ + depth + offsetZ);
    int startIndex = static_cast<int>(outVertices.size());
    outVertices.push_back(Vertex(xPos, Y0, Z0, r, g, b));
    outVertices.push_back(Vertex(xPos, Y0, Z1, r, g, b));
    outVertices.push_back(Vertex(xPos, Y1, Z1, r, g, b));
    outVertices.push_back(Vertex(xPos, Y1, Z0, r, g, b));
    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

// -X face (left)
static void buildQuadNegX(
    int startY, int startZ, int height, int depth, int x,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    float xPos = static_cast<float>(x + offsetX);
    float Y0 = static_cast<float>(startY + offsetY);
    float Z0 = static_cast<float>(startZ + offsetZ);
    float Y1 = static_cast<float>(startY + height + offsetY);
    float Z1 = static_cast<float>(startZ + depth + offsetZ);
    int startIndex = static_cast<int>(outVertices.size());
    // Reverse winding for -X face.
    outVertices.push_back(Vertex(xPos, Y0, Z1, r, g, b));
    outVertices.push_back(Vertex(xPos, Y0, Z0, r, g, b));
    outVertices.push_back(Vertex(xPos, Y1, Z0, r, g, b));
    outVertices.push_back(Vertex(xPos, Y1, Z1, r, g, b));
    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

// +Y face (top)
static void buildQuadPosY(
    int startX, int startZ, int width, int depth, int y,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    float yPos = static_cast<float>(y + 1 + offsetY);
    float X0 = static_cast<float>(startX + offsetX);
    float Z0 = static_cast<float>(startZ + offsetZ);
    float X1 = static_cast<float>(startX + width + offsetX);
    float Z1 = static_cast<float>(startZ + depth + offsetZ);
    int startIndex = static_cast<int>(outVertices.size());
    outVertices.push_back(Vertex(X0, yPos, Z0, r, g, b));
    outVertices.push_back(Vertex(X1, yPos, Z0, r, g, b));
    outVertices.push_back(Vertex(X1, yPos, Z1, r, g, b));
    outVertices.push_back(Vertex(X0, yPos, Z1, r, g, b));
    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

// -Y face (bottom)
static void buildQuadNegY(
    int startX, int startZ, int width, int depth, int y,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;
    float yPos = static_cast<float>(y + offsetY);
    float X0 = static_cast<float>(startX + offsetX);
    float Z0 = static_cast<float>(startZ + offsetZ);
    float X1 = static_cast<float>(startX + width + offsetX);
    float Z1 = static_cast<float>(startZ + depth + offsetZ);
    int startIndex = static_cast<int>(outVertices.size());
    // Reverse winding for -Y face.
    outVertices.push_back(Vertex(X1, yPos, Z0, r, g, b));
    outVertices.push_back(Vertex(X0, yPos, Z0, r, g, b));
    outVertices.push_back(Vertex(X0, yPos, Z1, r, g, b));
    outVertices.push_back(Vertex(X1, yPos, Z1, r, g, b));
    outIndices.push_back(startIndex + 0);
    outIndices.push_back(startIndex + 1);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 2);
    outIndices.push_back(startIndex + 3);
    outIndices.push_back(startIndex + 0);
}

//-------------------- Implementation of generateMesh for all faces --------------------

bool GreedyMesher::generateMesh(
    Chunk& chunk,
    int cx, int cy, int cz,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    int offsetX, int offsetY, int offsetZ,
    const ChunkManager& manager) const
{
    outVertices.clear();
    outIndices.clear();

    const int sizeX = Chunk::SIZE_X;
    const int sizeY = Chunk::SIZE_Y;
    const int sizeZ = Chunk::SIZE_Z;

    // Helper lambdas for solidity.
    auto isSolidID = [](int voxelID) -> bool {
        if (voxelID < 0) return false;
        const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
        return vt.isSolid;
        };

    auto isSolidGlobal = [&](int x, int y, int z) -> bool {
        if (x >= 0 && x < sizeX &&
            y >= 0 && y < sizeY &&
            z >= 0 && z < sizeZ)
        {
            int nid = chunk.getBlock(x, y, z);
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

    // Process each face direction separately.
    // ---------- +Z Faces ----------
    for (int z = 0; z < sizeZ; z++) {
        std::vector<int> mask(sizeX * sizeY, -1); // reinitialize per z-layer
        for (int y = 0; y < sizeY; y++) {
            for (int x = 0; x < sizeX; x++) {
                int id = chunk.getBlock(x, y, z);
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
                    if (mask[idx2] == currentID)
                        width++;
                    else
                        break;
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
                buildQuadPosZ(col, row, width, height, z, offsetX, offsetY, offsetZ, currentID, outVertices, outIndices);
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
                int id = chunk.getBlock(x, y, z);
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
                    if (mask[idx2] == currentID)
                        width++;
                    else
                        break;
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
                buildQuadNegZ(col, row, width, height, z, offsetX, offsetY, offsetZ, currentID, outVertices, outIndices);
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
                int id = chunk.getBlock(x, y, z);
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
                    if (mask[idx2] == currentID)
                        height++;
                    else
                        break;
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
                buildQuadPosX(col, row, height, depth, x, offsetX, offsetY, offsetZ, currentID, outVertices, outIndices);
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
                int id = chunk.getBlock(x, y, z);
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
                    if (mask[idx2] == currentID)
                        height++;
                    else
                        break;
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
                buildQuadNegX(col, row, height, depth, x, offsetX, offsetY, offsetZ, currentID, outVertices, outIndices);
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
                int id = chunk.getBlock(x, y, z);
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
                    if (mask[idx2] == currentID)
                        width++;
                    else
                        break;
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
                buildQuadPosY(col, row, width, depth, y, offsetX, offsetY, offsetZ, currentID, outVertices, outIndices);
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
                int id = chunk.getBlock(x, y, z);
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
                    if (mask[idx2] == currentID)
                        width++;
                    else
                        break;
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
                buildQuadNegY(col, row, width, depth, y, offsetX, offsetY, offsetZ, currentID, outVertices, outIndices);
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

    return !outVertices.empty();
}
