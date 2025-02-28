#include "ChunkMesher.h"
#include "VoxelTypeRegistry.h"
#include "VoxelType.h"
#include <stdexcept>
#include <iostream>

/**
 * Determines if a voxel ID is solid by checking the VoxelTypeRegistry.
 */
bool ChunkMesher::isSolidID(int voxelID)
{
    if (voxelID < 0) return false; // negative => treat as air
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
    return vt.isSolid;
}

/**
 * Checks if local coords (x,y,z) are solid, possibly in a neighbor chunk.
 * Used by naive and greedy meshing for adjacency checks.
 */
bool ChunkMesher::isSolidGlobal(
    const Chunk& currentChunk,
    int cx, int cy, int cz,
    int x, int y, int z,
    const ChunkManager& manager)
{
    // If in-range => check current chunk's data
    if (x >= 0 && x < Chunk::SIZE_X &&
        y >= 0 && y < Chunk::SIZE_Y &&
        z >= 0 && z < Chunk::SIZE_Z)
    {
        int id = currentChunk.getBlock(x, y, z);
        return (id > 0) ? isSolidID(id) : false;
    }
    else
    {
        // Out-of-bounds => neighbor chunk
        int nx = cx, ny = cy, nz = cz;
        int localX = x, localY = y, localZ = z;

        // Shift coords to neighbor
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

        // Grab neighbor
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
 * Naive mesh generation with adjacency checks.
 * For each voxel, if it’s solid and the neighbor is not solid,
 * we add a face for that side.
 */
void ChunkMesher::generateMeshNaive(
    const Chunk& chunk,
    int cx, int cy, int cz,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    int offsetX, int offsetY, int offsetZ,
    const ChunkManager& manager)
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

                // Color from the voxel type
                const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
                float r = vt.color.r;
                float g = vt.color.g;
                float b = vt.color.b;

                float baseX = float(x + offsetX);
                float baseY = float(y + offsetY);
                float baseZ = float(z + offsetZ);

                // +X
                if (!isSolidGlobal(chunk, cx, cy, cz, x + 1, y, z, manager))
                {
                    int startIdx = (int)outVertices.size();
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
                // -X
                if (!isSolidGlobal(chunk, cx, cy, cz, x - 1, y, z, manager))
                {
                    int startIdx = (int)outVertices.size();
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
                // +Y
                if (!isSolidGlobal(chunk, cx, cy, cz, x, y + 1, z, manager))
                {
                    int startIdx = (int)outVertices.size();
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
                // -Y
                if (!isSolidGlobal(chunk, cx, cy, cz, x, y - 1, z, manager))
                {
                    int startIdx = (int)outVertices.size();
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
                // +Z
                if (!isSolidGlobal(chunk, cx, cy, cz, x, y, z + 1, manager))
                {
                    int startIdx = (int)outVertices.size();
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
                // -Z
                if (!isSolidGlobal(chunk, cx, cy, cz, x, y, z - 1, manager))
                {
                    int startIdx = (int)outVertices.size();
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
}

/**
 * Checks if LOD0 is dirty and re-meshes if needed (naive or greedy).
 */
bool ChunkMesher::generateChunkMeshIfDirty(
    Chunk& chunk,
    int cx, int cy, int cz,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    int offsetX, int offsetY, int offsetZ,
    const ChunkManager& manager,
    bool useGreedy)
{
    if (!chunk.isDirty()) {
        return false; // no new mesh for LOD0
    }
    chunk.clearDirty(); // clear the LOD0 dirty flag

    outVertices.clear();
    outIndices.clear();

    if (useGreedy) {
        generateMeshGreedy(chunk, cx, cy, cz,
            outVertices, outIndices,
            offsetX, offsetY, offsetZ,
            manager);
    }
    else {
        generateMeshNaive(chunk, cx, cy, cz,
            outVertices, outIndices,
            offsetX, offsetY, offsetZ,
            manager);
    }
    return true;
}

/**
 * A simpler test method ignoring adjacency with neighbor chunks.
 * (Your original code.)
 */
void ChunkMesher::generateMeshNaiveTest(
    const Chunk& chunk,
    std::vector<Vertex>& outVerts,
    std::vector<uint32_t>& outInds,
    int offsetX, int offsetY, int offsetZ)
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

                float r = 0.5f, g = 0.5f, b = 0.5f; // Hard-coded color

                float bx = float(x + offsetX);
                float by = float(y + offsetY);
                float bz = float(z + offsetZ);

                // (+X) face
                {
                    int startIdx = (int)outVerts.size();
                    outVerts.push_back(Vertex(bx + 1, by, bz, r, g, b));
                    outVerts.push_back(Vertex(bx + 1, by, bz + 1, r, g, b));
                    outVerts.push_back(Vertex(bx + 1, by + 1, bz + 1, r, g, b));
                    outVerts.push_back(Vertex(bx + 1, by + 1, bz, r, g, b));

                    outInds.push_back(startIdx + 0);
                    outInds.push_back(startIdx + 1);
                    outInds.push_back(startIdx + 2);
                    outInds.push_back(startIdx + 2);
                    outInds.push_back(startIdx + 3);
                    outInds.push_back(startIdx + 0);
                }
                // (-X) face
                {
                    int startIdx = (int)outVerts.size();
                    outVerts.push_back(Vertex(bx, by, bz + 1, r, g, b));
                    outVerts.push_back(Vertex(bx, by, bz, r, g, b));
                    outVerts.push_back(Vertex(bx, by + 1, bz, r, g, b));
                    outVerts.push_back(Vertex(bx, by + 1, bz + 1, r, g, b));

                    outInds.push_back(startIdx + 0);
                    outInds.push_back(startIdx + 1);
                    outInds.push_back(startIdx + 2);
                    outInds.push_back(startIdx + 2);
                    outInds.push_back(startIdx + 3);
                    outInds.push_back(startIdx + 0);
                }
                // (+Y) face
                {
                    int startIdx = (int)outVerts.size();
                    outVerts.push_back(Vertex(bx, by + 1, bz, r, g, b));
                    outVerts.push_back(Vertex(bx + 1, by + 1, bz, r, g, b));
                    outVerts.push_back(Vertex(bx + 1, by + 1, bz + 1, r, g, b));
                    outVerts.push_back(Vertex(bx, by + 1, bz + 1, r, g, b));

                    outInds.push_back(startIdx + 0);
                    outInds.push_back(startIdx + 1);
                    outInds.push_back(startIdx + 2);
                    outInds.push_back(startIdx + 2);
                    outInds.push_back(startIdx + 3);
                    outInds.push_back(startIdx + 0);
                }
                // (-Y) face
                {
                    int startIdx = (int)outVerts.size();
                    outVerts.push_back(Vertex(bx + 1, by, bz, r, g, b));
                    outVerts.push_back(Vertex(bx, by, bz, r, g, b));
                    outVerts.push_back(Vertex(bx, by, bz + 1, r, g, b));
                    outVerts.push_back(Vertex(bx + 1, by, bz + 1, r, g, b));

                    outInds.push_back(startIdx + 0);
                    outInds.push_back(startIdx + 1);
                    outInds.push_back(startIdx + 2);
                    outInds.push_back(startIdx + 2);
                    outInds.push_back(startIdx + 3);
                    outInds.push_back(startIdx + 0);
                }
                // (+Z) face
                {
                    int startIdx = (int)outVerts.size();
                    outVerts.push_back(Vertex(bx, by, bz + 1, r, g, b));
                    outVerts.push_back(Vertex(bx + 1, by, bz + 1, r, g, b));
                    outVerts.push_back(Vertex(bx + 1, by + 1, bz + 1, r, g, b));
                    outVerts.push_back(Vertex(bx, by + 1, bz + 1, r, g, b));

                    outInds.push_back(startIdx + 0);
                    outInds.push_back(startIdx + 1);
                    outInds.push_back(startIdx + 2);
                    outInds.push_back(startIdx + 2);
                    outInds.push_back(startIdx + 3);
                    outInds.push_back(startIdx + 0);
                }
                // (-Z) face
                {
                    int startIdx = (int)outVerts.size();
                    outVerts.push_back(Vertex(bx + 1, by, bz, r, g, b));
                    outVerts.push_back(Vertex(bx, by, bz, r, g, b));
                    outVerts.push_back(Vertex(bx, by + 1, bz, r, g, b));
                    outVerts.push_back(Vertex(bx + 1, by + 1, bz, r, g, b));

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

/**
 * The "greedy" meshing approach for LOD0, merging faces in each direction.
 * (Same as your original code, with debug print at the end.)
 */
void ChunkMesher::generateMeshGreedy(
    const Chunk& chunk,
    int cx, int cy, int cz,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    int offsetX, int offsetY, int offsetZ,
    const ChunkManager& manager)
{
    outVertices.clear();
    outIndices.clear();

    // +Z direction
    for (int z = 0; z < Chunk::SIZE_Z; z++)
    {
        std::vector<int> mask(Chunk::SIZE_X * Chunk::SIZE_Y, -1);
        for (int y = 0; y < Chunk::SIZE_Y; y++)
        {
            for (int x = 0; x < Chunk::SIZE_X; x++)
            {
                int id = chunk.getBlock(x, y, z);
                if (id <= 0) continue;

                bool exposed = !isSolidGlobal(chunk, cx, cy, cz, x, y, z + 1, manager);
                if (exposed)
                {
                    size_t idx = static_cast<size_t>(y) * Chunk::SIZE_X + x;
                    mask[idx] = id;
                }
            }
        }
        for (int row = 0; row < Chunk::SIZE_Y; row++)
        {
            int col = 0;
            while (col < Chunk::SIZE_X)
            {
                size_t idx = static_cast<size_t>(row) * Chunk::SIZE_X + col;
                int bid = mask[idx];
                if (bid < 0)
                {
                    col++;
                    continue;
                }
                // find width
                int width = 1;
                while ((col + width) < Chunk::SIZE_X)
                {
                    size_t idx2 = static_cast<size_t>(row) * Chunk::SIZE_X + (col + width);
                    if (mask[idx2] == bid) width++; else break;
                }
                // find height
                int height = 1;
                bool done = false;
                while (!done)
                {
                    int nextRow = row + height;
                    if (nextRow >= Chunk::SIZE_Y) break;
                    for (int c2 = 0; c2 < width; c2++)
                    {
                        size_t idx3 = static_cast<size_t>(nextRow) * Chunk::SIZE_X + (col + c2);
                        if (mask[idx3] != bid)
                        {
                            done = true;
                            break;
                        }
                    }
                    if (!done) height++;
                }
                buildQuadPosZ(col, row, width, height, z,
                    offsetX, offsetY, offsetZ,
                    bid, outVertices, outIndices);

                // mark used
                for (int rr = 0; rr < height; rr++)
                {
                    for (int cc = 0; cc < width; cc++)
                    {
                        size_t idx4 = static_cast<size_t>(row + rr) * Chunk::SIZE_X + (col + cc);
                        mask[idx4] = -1;
                    }
                }
                col += width;
            }
        }
    }

    // -Z direction
    for (int z = 0; z < Chunk::SIZE_Z; z++)
    {
        std::vector<int> mask(Chunk::SIZE_X * Chunk::SIZE_Y, -1);
        for (int y = 0; y < Chunk::SIZE_Y; y++)
        {
            for (int x = 0; x < Chunk::SIZE_X; x++)
            {
                int id = chunk.getBlock(x, y, z);
                if (id <= 0) continue;

                bool exposed = !isSolidGlobal(chunk, cx, cy, cz, x, y, z - 1, manager);
                if (exposed)
                {
                    size_t idx = static_cast<size_t>(y) * Chunk::SIZE_X + x;
                    mask[idx] = id;
                }
            }
        }
        for (int row = 0; row < Chunk::SIZE_Y; row++)
        {
            int col = 0;
            while (col < Chunk::SIZE_X)
            {
                size_t idx = static_cast<size_t>(row) * Chunk::SIZE_X + col;
                int bid = mask[idx];
                if (bid < 0)
                {
                    col++;
                    continue;
                }
                int width = 1;
                while ((col + width) < Chunk::SIZE_X)
                {
                    size_t idx2 = static_cast<size_t>(row) * Chunk::SIZE_X + (col + width);
                    if (mask[idx2] == bid) width++; else break;
                }
                int height = 1;
                bool done = false;
                while (!done)
                {
                    int nextRow = row + height;
                    if (nextRow >= Chunk::SIZE_Y) break;
                    for (int c2 = 0; c2 < width; c2++)
                    {
                        size_t idx3 = static_cast<size_t>(nextRow) * Chunk::SIZE_X + (col + c2);
                        if (mask[idx3] != bid)
                        {
                            done = true;
                            break;
                        }
                    }
                    if (!done) height++;
                }
                buildQuadNegZ(col, row, width, height, z,
                    offsetX, offsetY, offsetZ,
                    bid, outVertices, outIndices);

                for (int rr = 0; rr < height; rr++)
                {
                    for (int cc = 0; cc < width; cc++)
                    {
                        size_t idx4 = static_cast<size_t>(row + rr) * Chunk::SIZE_X + (col + cc);
                        mask[idx4] = -1;
                    }
                }
                col += width;
            }
        }
    }

    // +X direction
    for (int x = 0; x < Chunk::SIZE_X; x++)
    {
        std::vector<int> mask(Chunk::SIZE_Y * Chunk::SIZE_Z, -1);
        for (int z = 0; z < Chunk::SIZE_Z; z++)
        {
            for (int y = 0; y < Chunk::SIZE_Y; y++)
            {
                int id = chunk.getBlock(x, y, z);
                if (id <= 0) continue;

                bool exposed = !isSolidGlobal(chunk, cx, cy, cz, x + 1, y, z, manager);
                if (exposed)
                {
                    size_t idx = static_cast<size_t>(z) * Chunk::SIZE_Y + y;
                    mask[idx] = id;
                }
            }
        }
        for (int startZ = 0; startZ < Chunk::SIZE_Z; startZ++)
        {
            int startY = 0;
            while (startY < Chunk::SIZE_Y)
            {
                size_t idx = static_cast<size_t>(startZ) * Chunk::SIZE_Y + startY;
                int bid = mask[idx];
                if (bid < 0) {
                    startY++;
                    continue;
                }
                int height = 1;
                while ((startY + height) < Chunk::SIZE_Y)
                {
                    size_t idx2 = static_cast<size_t>(startZ) * Chunk::SIZE_Y + (startY + height);
                    if (mask[idx2] == bid) height++; else break;
                }
                int depth = 1;
                bool done = false;
                while (!done)
                {
                    int nz = startZ + depth;
                    if (nz >= Chunk::SIZE_Z) break;
                    for (int hy = 0; hy < height; hy++)
                    {
                        size_t idx3 = static_cast<size_t>(nz) * Chunk::SIZE_Y + (startY + hy);
                        if (mask[idx3] != bid)
                        {
                            done = true;
                            break;
                        }
                    }
                    if (!done) depth++;
                }
                buildQuadPosX(startY, startZ, height, depth, x,
                    offsetX, offsetY, offsetZ,
                    bid, outVertices, outIndices);

                for (int dz = 0; dz < depth; dz++)
                {
                    for (int hy = 0; hy < height; hy++)
                    {
                        size_t idx4 = static_cast<size_t>(startZ + dz) * Chunk::SIZE_Y + (startY + hy);
                        mask[idx4] = -1;
                    }
                }
                startY += height;
            }
        }
    }

    // -X direction
    for (int x = 0; x < Chunk::SIZE_X; x++)
    {
        std::vector<int> mask(Chunk::SIZE_Y * Chunk::SIZE_Z, -1);
        for (int z = 0; z < Chunk::SIZE_Z; z++)
        {
            for (int y = 0; y < Chunk::SIZE_Y; y++)
            {
                int id = chunk.getBlock(x, y, z);
                if (id <= 0) continue;

                bool exposed = !isSolidGlobal(chunk, cx, cy, cz, x - 1, y, z, manager);
                if (exposed)
                {
                    size_t idx = static_cast<size_t>(z) * Chunk::SIZE_Y + y;
                    mask[idx] = id;
                }
            }
        }
        for (int startZ = 0; startZ < Chunk::SIZE_Z; startZ++)
        {
            int startY = 0;
            while (startY < Chunk::SIZE_Y)
            {
                size_t idx = static_cast<size_t>(startZ) * Chunk::SIZE_Y + startY;
                int bid = mask[idx];
                if (bid < 0) {
                    startY++;
                    continue;
                }
                int height = 1;
                while ((startY + height) < Chunk::SIZE_Y)
                {
                    size_t idx2 = static_cast<size_t>(startZ) * Chunk::SIZE_Y + (startY + height);
                    if (mask[idx2] == bid) height++; else break;
                }
                int depth = 1;
                bool done = false;
                while (!done)
                {
                    int nz = startZ + depth;
                    if (nz >= Chunk::SIZE_Z) break;
                    for (int hy = 0; hy < height; hy++)
                    {
                        size_t idx3 = static_cast<size_t>(nz) * Chunk::SIZE_Y + (startY + hy);
                        if (mask[idx3] != bid)
                        {
                            done = true;
                            break;
                        }
                    }
                    if (!done) depth++;
                }
                buildQuadNegX(startY, startZ, height, depth, x,
                    offsetX, offsetY, offsetZ,
                    bid, outVertices, outIndices);

                for (int dz = 0; dz < depth; dz++)
                {
                    for (int hy = 0; hy < height; hy++)
                    {
                        size_t idx4 = static_cast<size_t>(startZ + dz) * Chunk::SIZE_Y + (startY + hy);
                        mask[idx4] = -1;
                    }
                }
                startY += height;
            }
        }
    }

    // +Y direction
    for (int y = 0; y < Chunk::SIZE_Y; y++)
    {
        std::vector<int> mask(Chunk::SIZE_X * Chunk::SIZE_Z, -1);
        for (int z = 0; z < Chunk::SIZE_Z; z++)
        {
            for (int x = 0; x < Chunk::SIZE_X; x++)
            {
                int id = chunk.getBlock(x, y, z);
                if (id <= 0) continue;

                bool exposed = !isSolidGlobal(chunk, cx, cy, cz, x, y + 1, z, manager);
                if (exposed)
                {
                    size_t idx = static_cast<size_t>(z) * Chunk::SIZE_X + x;
                    mask[idx] = id;
                }
            }
        }
        for (int startZ = 0; startZ < Chunk::SIZE_Z; startZ++)
        {
            int startX = 0;
            while (startX < Chunk::SIZE_X)
            {
                size_t idx = static_cast<size_t>(startZ) * Chunk::SIZE_X + startX;
                int bid = mask[idx];
                if (bid < 0) {
                    startX++;
                    continue;
                }
                int width = 1;
                while ((startX + width) < Chunk::SIZE_X)
                {
                    size_t idx2 = static_cast<size_t>(startZ) * Chunk::SIZE_X + (startX + width);
                    if (mask[idx2] == bid) width++; else break;
                }
                int depth = 1;
                bool done = false;
                while (!done)
                {
                    int nz = startZ + depth;
                    if (nz >= Chunk::SIZE_Z) break;
                    for (int wx = 0; wx < width; wx++)
                    {
                        size_t idx3 = static_cast<size_t>(nz) * Chunk::SIZE_X + (startX + wx);
                        if (mask[idx3] != bid)
                        {
                            done = true;
                            break;
                        }
                    }
                    if (!done) depth++;
                }
                buildQuadPosY(startX, startZ, width, depth, y,
                    offsetX, offsetY, offsetZ,
                    bid, outVertices, outIndices);

                for (int dz = 0; dz < depth; dz++)
                {
                    for (int wx = 0; wx < width; wx++)
                    {
                        size_t idx4 = static_cast<size_t>(startZ + dz) * Chunk::SIZE_X + (startX + wx);
                        mask[idx4] = -1;
                    }
                }
                startX += width;
            }
        }
    }

    // -Y direction
    for (int y = 0; y < Chunk::SIZE_Y; y++)
    {
        std::vector<int> mask(Chunk::SIZE_X * Chunk::SIZE_Z, -1);
        for (int z = 0; z < Chunk::SIZE_Z; z++)
        {
            for (int x = 0; x < Chunk::SIZE_X; x++)
            {
                int id = chunk.getBlock(x, y, z);
                if (id <= 0) continue;

                bool exposed = !isSolidGlobal(chunk, cx, cy, cz, x, y - 1, z, manager);
                if (exposed)
                {
                    size_t idx = static_cast<size_t>(z) * Chunk::SIZE_X + x;
                    mask[idx] = id;
                }
            }
        }
        for (int startZ = 0; startZ < Chunk::SIZE_Z; startZ++)
        {
            int startX = 0;
            while (startX < Chunk::SIZE_X)
            {
                size_t idx = static_cast<size_t>(startZ) * Chunk::SIZE_X + startX;
                int bid = mask[idx];
                if (bid < 0) {
                    startX++;
                    continue;
                }
                int width = 1;
                while ((startX + width) < Chunk::SIZE_X)
                {
                    size_t idx2 = static_cast<size_t>(startZ) * Chunk::SIZE_X + (startX + width);
                    if (mask[idx2] == bid) width++; else break;
                }
                int depth = 1;
                bool done = false;
                while (!done)
                {
                    int nz = startZ + depth;
                    if (nz >= Chunk::SIZE_Z) break;
                    for (int wx = 0; wx < width; wx++)
                    {
                        size_t idx3 = static_cast<size_t>(nz) * Chunk::SIZE_X + (startX + wx);
                        if (mask[idx3] != bid)
                        {
                            done = true;
                            break;
                        }
                    }
                    if (!done) depth++;
                }
                buildQuadNegY(startX, startZ, width, depth, y,
                    offsetX, offsetY, offsetZ,
                    bid, outVertices, outIndices);

                for (int dz = 0; dz < depth; dz++)
                {
                    for (int wx = 0; wx < width; wx++)
                    {
                        size_t idx4 = static_cast<size_t>(startZ + dz) * Chunk::SIZE_X + (startX + wx);
                        mask[idx4] = -1;
                    }
                }
                startX += width;
            }
        }
    }

    // Debug output
    std::cout << "[Mesh Debug] Chunk(" << cx << "," << cy << "," << cz
        << ") => " << outVertices.size() << " verts, "
        << outIndices.size() << " inds\n";
}

/**
 * Builds a mesh from a generic in-memory voxel array of size dsX * dsY * dsZ,
 * which can be used for LOD > 0 (downsampled arrays).
 * If useGreedy == true, you’d adapt your greedy approach to these smaller dims.
 */
void ChunkMesher::generateMeshFromArray(
    const std::vector<int>& voxelArray,
    int dsX, int dsY, int dsZ,
    int worldOffsetX, int worldOffsetY, int worldOffsetZ,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    bool useGreedy
)
{
    outVertices.clear();
    outIndices.clear();

    if (!useGreedy)
    {
        // Simple naive adjacency within dsX, dsY, dsZ (no neighbor chunks).
        for (int x = 0; x < dsX; x++)
        {
            for (int y = 0; y < dsY; y++)
            {
                for (int z = 0; z < dsZ; z++)
                {
                    int voxelID = voxelArray[x + dsX * (y + dsY * z)];
                    if (voxelID <= 0) continue; // skip air

                    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
                    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

                    float bx = float(x + worldOffsetX);
                    float by = float(y + worldOffsetY);
                    float bz = float(z + worldOffsetZ);

                    // A small lambda to check if the neighbor is solid (in-bounds).
                    auto getBlock = [&](int xx, int yy, int zz) -> int {
                        if (xx < 0 || xx >= dsX ||
                            yy < 0 || yy >= dsY ||
                            zz < 0 || zz >= dsZ)
                        {
                            return -1; // treat out-of-range as air
                        }
                        return voxelArray[xx + dsX * (yy + dsY * zz)];
                        };

                    // +X
                    if (!isSolidID(getBlock(x + 1, y, z))) {
                        int startIdx = (int)outVertices.size();
                        outVertices.push_back(Vertex(bx + 1, by, bz, r, g, b));
                        outVertices.push_back(Vertex(bx + 1, by, bz + 1, r, g, b));
                        outVertices.push_back(Vertex(bx + 1, by + 1, bz + 1, r, g, b));
                        outVertices.push_back(Vertex(bx + 1, by + 1, bz, r, g, b));

                        outIndices.push_back(startIdx + 0);
                        outIndices.push_back(startIdx + 1);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 3);
                        outIndices.push_back(startIdx + 0);
                    }
                    // -X
                    if (!isSolidID(getBlock(x - 1, y, z))) {
                        int startIdx = (int)outVertices.size();
                        outVertices.push_back(Vertex(bx, by, bz + 1, r, g, b));
                        outVertices.push_back(Vertex(bx, by, bz, r, g, b));
                        outVertices.push_back(Vertex(bx, by + 1, bz, r, g, b));
                        outVertices.push_back(Vertex(bx, by + 1, bz + 1, r, g, b));

                        outIndices.push_back(startIdx + 0);
                        outIndices.push_back(startIdx + 1);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 3);
                        outIndices.push_back(startIdx + 0);
                    }
                    // +Y
                    if (!isSolidID(getBlock(x, y + 1, z))) {
                        int startIdx = (int)outVertices.size();
                        outVertices.push_back(Vertex(bx, by + 1, bz, r, g, b));
                        outVertices.push_back(Vertex(bx + 1, by + 1, bz, r, g, b));
                        outVertices.push_back(Vertex(bx + 1, by + 1, bz + 1, r, g, b));
                        outVertices.push_back(Vertex(bx, by + 1, bz + 1, r, g, b));

                        outIndices.push_back(startIdx + 0);
                        outIndices.push_back(startIdx + 1);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 3);
                        outIndices.push_back(startIdx + 0);
                    }
                    // -Y
                    if (!isSolidID(getBlock(x, y - 1, z))) {
                        int startIdx = (int)outVertices.size();
                        outVertices.push_back(Vertex(bx + 1, by, bz, r, g, b));
                        outVertices.push_back(Vertex(bx, by, bz, r, g, b));
                        outVertices.push_back(Vertex(bx, by, bz + 1, r, g, b));
                        outVertices.push_back(Vertex(bx + 1, by, bz + 1, r, g, b));

                        outIndices.push_back(startIdx + 0);
                        outIndices.push_back(startIdx + 1);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 3);
                        outIndices.push_back(startIdx + 0);
                    }
                    // +Z
                    if (!isSolidID(getBlock(x, y, z + 1))) {
                        int startIdx = (int)outVertices.size();
                        outVertices.push_back(Vertex(bx, by, bz + 1, r, g, b));
                        outVertices.push_back(Vertex(bx + 1, by, bz + 1, r, g, b));
                        outVertices.push_back(Vertex(bx + 1, by + 1, bz + 1, r, g, b));
                        outVertices.push_back(Vertex(bx, by + 1, bz + 1, r, g, b));

                        outIndices.push_back(startIdx + 0);
                        outIndices.push_back(startIdx + 1);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 2);
                        outIndices.push_back(startIdx + 3);
                        outIndices.push_back(startIdx + 0);
                    }
                    // -Z
                    if (!isSolidID(getBlock(x, y, z - 1))) {
                        int startIdx = (int)outVertices.size();
                        outVertices.push_back(Vertex(bx + 1, by, bz, r, g, b));
                        outVertices.push_back(Vertex(bx, by, bz, r, g, b));
                        outVertices.push_back(Vertex(bx, by + 1, bz, r, g, b));
                        outVertices.push_back(Vertex(bx + 1, by + 1, bz, r, g, b));

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
    else
    {
        // (Optional) If you want a "greedy" approach for LOD data,
        // adapt your code from generateMeshGreedy to handle dsX, dsY, dsZ, 
        // ignoring neighbor chunks. Omitted here for brevity.
    }
}

/**
 * These buildQuadPosZ, buildQuadNegZ, etc. are used by the greedy approach
 * to add quads after merging. They’re defined below.
 * - See your original code for the specifics. We keep them unchanged.
 */

 // +Z
void ChunkMesher::buildQuadPosZ(
    int startX, int startY, int width, int height, int z,
    int offsetX, int offsetY, int offsetZ, int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices
)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

    float zPos = float(z + 1 + offsetZ);
    float X0 = float(startX + offsetX);
    float Y0 = float(startY + offsetY);
    float X1 = float(startX + width + offsetX);
    float Y1 = float(startY + height + offsetY);

    int startIndex = (int)outVertices.size();
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

// -Z
void ChunkMesher::buildQuadNegZ(
    int startX, int startY, int width, int height, int z,
    int offsetX, int offsetY, int offsetZ, int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices
)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

    float zPos = float(z + offsetZ);

    float X0 = float(startX + offsetX);
    float Y0 = float(startY + offsetY);
    float X1 = float(startX + width + offsetX);
    float Y1 = float(startY + height + offsetY);

    int startIndex = (int)outVertices.size();
    // Winding so that normal faces -Z
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

// +X
void ChunkMesher::buildQuadPosX(
    int startY, int startZ, int height, int depth, int x,
    int offsetX, int offsetY, int offsetZ, int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices
)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

    float xPos = float((x + 1) + offsetX);
    float Y0 = float(startY + offsetY);
    float Z0 = float(startZ + offsetZ);
    float Y1 = float(startY + height + offsetY);
    float Z1 = float(startZ + depth + offsetZ);

    int startIndex = (int)outVertices.size();
    // +X => normal in +X
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

// -X
void ChunkMesher::buildQuadNegX(
    int startY, int startZ, int height, int depth, int x,
    int offsetX, int offsetY, int offsetZ, int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices
)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

    float xPos = float(x + offsetX);
    float Y0 = float(startY + offsetY);
    float Z0 = float(startZ + offsetZ);
    float Y1 = float(startY + height + offsetY);
    float Z1 = float(startZ + depth + offsetZ);

    int startIndex = (int)outVertices.size();
    // -X => normal is -X
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

// +Y
void ChunkMesher::buildQuadPosY(
    int startX, int startZ, int width, int depth, int y,
    int offsetX, int offsetY, int offsetZ, int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices
)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

    float yPos = float((y + 1) + offsetY);
    float X0 = float(startX + offsetX);
    float Z0 = float(startZ + offsetZ);
    float X1 = float(startX + width + offsetX);
    float Z1 = float(startZ + depth + offsetZ);

    int startIndex = (int)outVertices.size();
    // +Y => normal is +Y
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

// -Y
void ChunkMesher::buildQuadNegY(
    int startX, int startZ, int width, int depth, int y,
    int offsetX, int offsetY, int offsetZ, int blockID,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices
)
{
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

    float yPos = float(y + offsetY);
    float X0 = float(startX + offsetX);
    float Z0 = float(startZ + offsetZ);
    float X1 = float(startX + width + offsetX);
    float Z1 = float(startZ + depth + offsetZ);

    int startIndex = (int)outVertices.size();
    // -Y => normal is -Y
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
