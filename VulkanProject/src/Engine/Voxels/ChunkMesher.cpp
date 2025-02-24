#include "ChunkMesher.h"
#include "Chunk.h"
#include "VoxelTypeRegistry.h"
#include "VoxelType.h"
#include <stdexcept>
#include <iostream>

//----------------------------------------------
// isSolidID: checks if voxelID => a solid VoxelType
//----------------------------------------------
bool ChunkMesher::isSolidID(int voxelID)
{
    if (voxelID < 0) return false; // negative => treat as air
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(voxelID);
    return vt.isSolid;
}

//----------------------------------------------
// isSolidGlobal: checks if local coords (x,y,z)
// are solid, possibly in neighbor chunk
//----------------------------------------------
bool ChunkMesher::isSolidGlobal(
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

//----------------------------------------------
// generateMeshNaive: adjacency checks
//----------------------------------------------
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

                // color
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

//----------------------------------------------
// generateChunkMeshIfDirty
//----------------------------------------------
bool ChunkMesher::generateChunkMeshIfDirty(
    Chunk& chunk,
    int cx, int cy, int cz,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    int offsetX, int offsetY, int offsetZ,
    const ChunkManager& manager,
    bool useGreedy
)
{
    // 1) Check dirty
    if (!chunk.isDirty()) {
        return false; // no new mesh
    }

    // 2) Clear dirty
    chunk.clearDirty();

    // 3) Generate geometry
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

    // returns true if we did re-mesh (even if empty)
    return true;
}

//----------------------------------------------
// buildQuadPosZ
//----------------------------------------------
void ChunkMesher::buildQuadPosZ(
    int startX, int startY, int width, int height, int z,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
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

//----------------------------------------------
// buildQuadNegZ
//----------------------------------------------
void ChunkMesher::buildQuadNegZ(
    int startX, int startY, int width, int height, int z,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
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
    // Winding the vertices so normal faces -Z
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

//----------------------------------------------
// buildQuadPosX
//----------------------------------------------
void ChunkMesher::buildQuadPosX(
    int startY, int startZ, int height, int depth, int x,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
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

//----------------------------------------------
// buildQuadNegX
//----------------------------------------------
void ChunkMesher::buildQuadNegX(
    int startY, int startZ, int height, int depth, int x,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
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

//----------------------------------------------
// buildQuadPosY
//----------------------------------------------
void ChunkMesher::buildQuadPosY(
    int startX, int startZ, int width, int depth, int y,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
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

//----------------------------------------------
// buildQuadNegY
//----------------------------------------------
void ChunkMesher::buildQuadNegY(
    int startX, int startZ, int width, int depth, int y,
    int offsetX, int offsetY, int offsetZ,
    int blockID,
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

//----------------------------------------------
// generateMeshGreedy
//----------------------------------------------
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

    //------------------------------------------------
    // 1) +Z direction
    //------------------------------------------------
    for (int z = 0; z < Chunk::SIZE_Z; z++)
    {
        // mask is SIZE_X * SIZE_Y
        std::vector<int> mask(Chunk::SIZE_X * Chunk::SIZE_Y, -1);

        // Fill mask
        for (int y = 0; y < Chunk::SIZE_Y; y++)
        {
            for (int x = 0; x < Chunk::SIZE_X; x++)
            {
                int id = chunk.getBlock(x, y, z);
                if (id <= 0) continue;

                bool exposed = !isSolidGlobal(chunk, cx, cy, cz, x, y, z + 1, manager);
                if (exposed)
                {
                    // Casting to size_t to avoid overflow
                    size_t idx = static_cast<size_t>(y) * static_cast<size_t>(Chunk::SIZE_X)
                        + static_cast<size_t>(x);
                    mask[idx] = id;
                }
            }
        }

        // Merge
        for (int row = 0; row < Chunk::SIZE_Y; row++)
        {
            int col = 0;
            while (col < Chunk::SIZE_X)
            {
                // index for mask
                size_t idx = static_cast<size_t>(row) * static_cast<size_t>(Chunk::SIZE_X)
                    + static_cast<size_t>(col);

                int bid = mask[idx];
                if (bid < 0) {
                    col++;
                    continue;
                }

                // find width
                int width = 1;
                while ((col + width) < Chunk::SIZE_X)
                {
                    size_t idx2 = static_cast<size_t>(row) * static_cast<size_t>(Chunk::SIZE_X)
                        + static_cast<size_t>(col + width);
                    if (mask[idx2] == bid) {
                        width++;
                    }
                    else {
                        break;
                    }
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
                        size_t idx3 = static_cast<size_t>(nextRow) * static_cast<size_t>(Chunk::SIZE_X)
                            + static_cast<size_t>(col + c2);
                        if (mask[idx3] != bid) {
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
                        size_t idx4 = static_cast<size_t>(row + rr) * static_cast<size_t>(Chunk::SIZE_X)
                            + static_cast<size_t>(col + cc);
                        mask[idx4] = -1;
                    }
                }

                col += width;
            }
        }
    }

    //------------------------------------------------
    // 2) -Z direction
    //------------------------------------------------
    for (int z = 0; z < Chunk::SIZE_Z; z++)
    {
        std::vector<int> mask(Chunk::SIZE_X * Chunk::SIZE_Y, -1);

        // fill
        for (int y = 0; y < Chunk::SIZE_Y; y++)
        {
            for (int x = 0; x < Chunk::SIZE_X; x++)
            {
                int id = chunk.getBlock(x, y, z);
                if (id <= 0) continue;

                bool exposed = !isSolidGlobal(chunk, cx, cy, cz, x, y, z - 1, manager);
                if (exposed)
                {
                    size_t idx = static_cast<size_t>(y) * static_cast<size_t>(Chunk::SIZE_X)
                        + static_cast<size_t>(x);
                    mask[idx] = id;
                }
            }
        }

        // merge
        for (int row = 0; row < Chunk::SIZE_Y; row++)
        {
            int col = 0;
            while (col < Chunk::SIZE_X)
            {
                size_t idx = static_cast<size_t>(row) * static_cast<size_t>(Chunk::SIZE_X)
                    + static_cast<size_t>(col);
                int bid = mask[idx];
                if (bid < 0) {
                    col++;
                    continue;
                }

                int width = 1;
                while ((col + width) < Chunk::SIZE_X)
                {
                    size_t idx2 = static_cast<size_t>(row) * static_cast<size_t>(Chunk::SIZE_X)
                        + static_cast<size_t>(col + width);
                    if (mask[idx2] == bid) {
                        width++;
                    }
                    else {
                        break;
                    }
                }

                int height = 1;
                bool done = false;
                while (!done)
                {
                    int nextRow = row + height;
                    if (nextRow >= Chunk::SIZE_Y) break;

                    for (int c2 = 0; c2 < width; c2++)
                    {
                        size_t idx3 = static_cast<size_t>(nextRow) * static_cast<size_t>(Chunk::SIZE_X)
                            + static_cast<size_t>(col + c2);
                        if (mask[idx3] != bid) {
                            done = true;
                            break;
                        }
                    }
                    if (!done) height++;
                }

                buildQuadNegZ(col, row, width, height, z,
                    offsetX, offsetY, offsetZ, bid,
                    outVertices, outIndices);

                // mark used
                for (int rr = 0; rr < height; rr++)
                {
                    for (int cc = 0; cc < width; cc++)
                    {
                        size_t idx4 = static_cast<size_t>(row + rr) * static_cast<size_t>(Chunk::SIZE_X)
                            + static_cast<size_t>(col + cc);
                        mask[idx4] = -1;
                    }
                }

                col += width;
            }
        }
    }

    //------------------------------------------------
    // 3) +X direction
    //------------------------------------------------
    for (int x = 0; x < Chunk::SIZE_X; x++)
    {
        // mask is SIZE_Y * SIZE_Z
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
                    size_t idx = static_cast<size_t>(z) * static_cast<size_t>(Chunk::SIZE_Y)
                        + static_cast<size_t>(y);
                    mask[idx] = id;
                }
            }
        }

        // Merge
        for (int startZ = 0; startZ < Chunk::SIZE_Z; startZ++)
        {
            int startY = 0;
            while (startY < Chunk::SIZE_Y)
            {
                size_t idx = static_cast<size_t>(startZ) * static_cast<size_t>(Chunk::SIZE_Y)
                    + static_cast<size_t>(startY);
                int bid = mask[idx];
                if (bid < 0) {
                    startY++;
                    continue;
                }

                // find height
                int height = 1;
                while ((startY + height) < Chunk::SIZE_Y)
                {
                    size_t idx2 = static_cast<size_t>(startZ) * static_cast<size_t>(Chunk::SIZE_Y)
                        + static_cast<size_t>(startY + height);
                    if (mask[idx2] == bid) {
                        height++;
                    }
                    else {
                        break;
                    }
                }

                // find depth
                int depth = 1;
                bool done = false;
                while (!done)
                {
                    int nextZ = startZ + depth;
                    if (nextZ >= Chunk::SIZE_Z) break;

                    for (int hy = 0; hy < height; hy++)
                    {
                        size_t idx3 = static_cast<size_t>(nextZ) * static_cast<size_t>(Chunk::SIZE_Y)
                            + static_cast<size_t>(startY + hy);
                        if (mask[idx3] != bid) {
                            done = true;
                            break;
                        }
                    }
                    if (!done) depth++;
                }

                buildQuadPosX(startY, startZ, height, depth, x,
                    offsetX, offsetY, offsetZ, bid,
                    outVertices, outIndices);

                // mark used
                for (int dz = 0; dz < depth; dz++)
                {
                    for (int hy = 0; hy < height; hy++)
                    {
                        size_t idx4 = static_cast<size_t>(startZ + dz) * static_cast<size_t>(Chunk::SIZE_Y)
                            + static_cast<size_t>(startY + hy);
                        mask[idx4] = -1;
                    }
                }

                startY += height;
            }
        }
    }

    //------------------------------------------------
    // 4) -X direction
    //------------------------------------------------
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
                    size_t idx = static_cast<size_t>(z) * static_cast<size_t>(Chunk::SIZE_Y)
                        + static_cast<size_t>(y);
                    mask[idx] = id;
                }
            }
        }

        // merge
        for (int startZ = 0; startZ < Chunk::SIZE_Z; startZ++)
        {
            int startY = 0;
            while (startY < Chunk::SIZE_Y)
            {
                size_t idx = static_cast<size_t>(startZ) * static_cast<size_t>(Chunk::SIZE_Y)
                    + static_cast<size_t>(startY);
                int bid = mask[idx];
                if (bid < 0) {
                    startY++;
                    continue;
                }

                int height = 1;
                while ((startY + height) < Chunk::SIZE_Y)
                {
                    size_t idx2 = static_cast<size_t>(startZ) * static_cast<size_t>(Chunk::SIZE_Y)
                        + static_cast<size_t>(startY + height);
                    if (mask[idx2] == bid) {
                        height++;
                    }
                    else {
                        break;
                    }
                }

                int depth = 1;
                bool done = false;
                while (!done)
                {
                    int nextZ = startZ + depth;
                    if (nextZ >= Chunk::SIZE_Z) break;

                    for (int hy = 0; hy < height; hy++)
                    {
                        size_t idx3 = static_cast<size_t>(nextZ) * static_cast<size_t>(Chunk::SIZE_Y)
                            + static_cast<size_t>(startY + hy);
                        if (mask[idx3] != bid) {
                            done = true;
                            break;
                        }
                    }
                    if (!done) depth++;
                }

                buildQuadNegX(startY, startZ, height, depth, x,
                    offsetX, offsetY, offsetZ, bid,
                    outVertices, outIndices);

                for (int dz = 0; dz < depth; dz++)
                {
                    for (int hy = 0; hy < height; hy++)
                    {
                        size_t idx4 = static_cast<size_t>(startZ + dz) * static_cast<size_t>(Chunk::SIZE_Y)
                            + static_cast<size_t>(startY + hy);
                        mask[idx4] = -1;
                    }
                }

                startY += height;
            }
        }
    }

    //------------------------------------------------
    // 5) +Y direction
    //------------------------------------------------
    for (int y = 0; y < Chunk::SIZE_Y; y++)
    {
        // mask is SIZE_X * SIZE_Z
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
                    size_t idx = static_cast<size_t>(z) * static_cast<size_t>(Chunk::SIZE_X)
                        + static_cast<size_t>(x);
                    mask[idx] = id;
                }
            }
        }

        // merge
        for (int startZ = 0; startZ < Chunk::SIZE_Z; startZ++)
        {
            int startX = 0;
            while (startX < Chunk::SIZE_X)
            {
                size_t idx = static_cast<size_t>(startZ) * static_cast<size_t>(Chunk::SIZE_X)
                    + static_cast<size_t>(startX);
                int bid = mask[idx];
                if (bid < 0) {
                    startX++;
                    continue;
                }

                int width = 1;
                while ((startX + width) < Chunk::SIZE_X)
                {
                    size_t idx2 = static_cast<size_t>(startZ) * static_cast<size_t>(Chunk::SIZE_X)
                        + static_cast<size_t>(startX + width);
                    if (mask[idx2] == bid) {
                        width++;
                    }
                    else {
                        break;
                    }
                }

                int depth = 1;
                bool done = false;
                while (!done)
                {
                    int nextZ = startZ + depth;
                    if (nextZ >= Chunk::SIZE_Z) break;

                    for (int wx = 0; wx < width; wx++)
                    {
                        size_t idx3 = static_cast<size_t>(nextZ) * static_cast<size_t>(Chunk::SIZE_X)
                            + static_cast<size_t>(startX + wx);
                        if (mask[idx3] != bid) {
                            done = true;
                            break;
                        }
                    }
                    if (!done) depth++;
                }

                buildQuadPosY(startX, startZ, width, depth, y,
                    offsetX, offsetY, offsetZ, bid,
                    outVertices, outIndices);

                // mark used
                for (int dz = 0; dz < depth; dz++)
                {
                    for (int wx = 0; wx < width; wx++)
                    {
                        size_t idx4 = static_cast<size_t>(startZ + dz) * static_cast<size_t>(Chunk::SIZE_X)
                            + static_cast<size_t>(startX + wx);
                        mask[idx4] = -1;
                    }
                }

                startX += width;
            }
        }
    }

    //------------------------------------------------
    // 6) -Y direction
    //------------------------------------------------
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
                    size_t idx = static_cast<size_t>(z) * static_cast<size_t>(Chunk::SIZE_X)
                        + static_cast<size_t>(x);
                    mask[idx] = id;
                }
            }
        }

        // merge
        for (int startZ = 0; startZ < Chunk::SIZE_Z; startZ++)
        {
            int startX = 0;
            while (startX < Chunk::SIZE_X)
            {
                size_t idx = static_cast<size_t>(startZ) * static_cast<size_t>(Chunk::SIZE_X)
                    + static_cast<size_t>(startX);
                int bid = mask[idx];
                if (bid < 0) {
                    startX++;
                    continue;
                }

                int width = 1;
                while ((startX + width) < Chunk::SIZE_X)
                {
                    size_t idx2 = static_cast<size_t>(startZ) * static_cast<size_t>(Chunk::SIZE_X)
                        + static_cast<size_t>(startX + width);
                    if (mask[idx2] == bid) {
                        width++;
                    }
                    else {
                        break;
                    }
                }

                int depth = 1;
                bool done = false;
                while (!done)
                {
                    int nextZ = startZ + depth;
                    if (nextZ >= Chunk::SIZE_Z) break;

                    for (int wx = 0; wx < width; wx++)
                    {
                        size_t idx3 = static_cast<size_t>(nextZ) * static_cast<size_t>(Chunk::SIZE_X)
                            + static_cast<size_t>(startX + wx);
                        if (mask[idx3] != bid) {
                            done = true;
                            break;
                        }
                    }
                    if (!done) depth++;
                }

                buildQuadNegY(startX, startZ, width, depth, y,
                    offsetX, offsetY, offsetZ, bid,
                    outVertices, outIndices);

                for (int dz = 0; dz < depth; dz++)
                {
                    for (int wx = 0; wx < width; wx++)
                    {
                        size_t idx4 = static_cast<size_t>(startZ + dz) * static_cast<size_t>(Chunk::SIZE_X)
                            + static_cast<size_t>(startX + wx);
                        mask[idx4] = -1;
                    }
                }

                startX += width;
            }
        }
    }
    //insert a debug to show the verts and chunks ind for chunk
    std::cout << "[Mesh Debug] Chunk(" << cx << "," << cy << "," << cz
        << ") => " << outVertices.size() << " verts, "
        << outIndices.size() << " inds\n";
    // Done! outVertices/outIndices => fully greedy-merged geometry
}

//----------------------------------------------
// generateMeshNaiveTest (ignores adjacency)
//----------------------------------------------
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