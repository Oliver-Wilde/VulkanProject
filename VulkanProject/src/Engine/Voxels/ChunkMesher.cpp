#include "ChunkMesher.h"
#include "VoxelTypeRegistry.h"
#include "VoxelType.h"
#include <stdexcept>
#include <iostream>



/**
 * NEW helper: returns the actual block ID in current or neighbor chunk,
 * or if the neighbor chunk doesn’t exist, we treat it as the same ID
 * so we don't show a boundary face for a missing neighbor.
 */
int ChunkMesher::getBlockIDGlobal(
    const Chunk& currentChunk,
    int cx, int cy, int cz,
    int x, int y, int z,
    const ChunkManager& manager)
{
    // If in-range => read from current chunk
    if (x >= 0 && x < Chunk::SIZE_X &&
        y >= 0 && y < Chunk::SIZE_Y &&
        z >= 0 && z < Chunk::SIZE_Z)
    {
        return currentChunk.getBlock(x, y, z);
    }
    else
    {
        // Out-of-bounds => find neighbor
        int nx = cx, ny = cy, nz = cz;
        int lx = x, ly = y, lz = z;

        // Shift local coords so they fall within the neighbor
        if (lx < 0) {
            nx -= 1;  lx += Chunk::SIZE_X;
        }
        else if (lx >= Chunk::SIZE_X) {
            nx += 1;  lx -= Chunk::SIZE_X;
        }
        if (ly < 0) {
            ny -= 1;  ly += Chunk::SIZE_Y;
        }
        else if (ly >= Chunk::SIZE_Y) {
            ny += 1;  ly -= Chunk::SIZE_Y;
        }
        if (lz < 0) {
            nz -= 1;  lz += Chunk::SIZE_Z;
        }
        else if (lz >= Chunk::SIZE_Z) {
            nz += 1;  lz -= Chunk::SIZE_Z;
        }

        const Chunk* neighbor = manager.getChunk(nx, ny, nz);
        if (!neighbor) {
            // If neighbor not loaded, treat it as the same ID => no boundary face
            return 0;
        }
        return neighbor->getBlock(lx, ly, lz);
    }
}


/**
 * Checks if LOD0 is dirty and re-meshes if needed (naive or greedy).
 * (Unchanged except it calls generateMeshNaive or generateMeshGreedy
 * which now do boundary merging by ID.)
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
        generateMeshGreedy(
            chunk, cx, cy, cz,
            outVertices, outIndices,
            offsetX, offsetY, offsetZ, manager
        );
    }
    /*else {
        generateMeshNaive(
            chunk, cx, cy, cz,
            outVertices, outIndices,
            offsetX, offsetY, offsetZ,
            manager
        );}*/
    
    return true;
}


/**
 * The "greedy" meshing approach for LOD0, merging faces internally. We now also do
 * boundary merging by checking for neighbor’s block ID != our own.
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
        // Instead of checking isSolidGlobal(..., z+1), we compare ID
        std::vector<int> mask(Chunk::SIZE_X * Chunk::SIZE_Y, -1);
        for (int y = 0; y < Chunk::SIZE_Y; y++)
        {
            for (int x = 0; x < Chunk::SIZE_X; x++)
            {
                int id = chunk.getBlock(x, y, z);
                if (id <= 0) continue;

                int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x, y, z + 1, manager);
                bool exposed = (neighborID != id);
                if (exposed)
                {
                    size_t idx = static_cast<size_t>(y) * Chunk::SIZE_X + x;
                    mask[idx] = id;
                }
            }
        }
        // Then the standard "greedy" pass (width/height).
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
                    if (mask[idx2] == bid) width++;
                    else break;
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
                // Build the +Z quad
                buildQuadPosZ(
                    col, row, width, height, z,
                    offsetX, offsetY, offsetZ, bid,
                    outVertices, outIndices
                );

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

                int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x, y, z - 1, manager);
                bool exposed = (neighborID != id);
                if (exposed)
                {
                    size_t idx = static_cast<size_t>(y) * Chunk::SIZE_X + x;
                    mask[idx] = id;
                }
            }
        }
        // The normal “greedy” row-by-row
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
                    if (mask[idx2] == bid) width++;
                    else break;
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
                buildQuadNegZ(
                    col, row, width, height, z,
                    offsetX, offsetY, offsetZ, bid,
                    outVertices, outIndices
                );

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

                int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x + 1, y, z, manager);
                bool exposed = (neighborID != id);
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
                    if (mask[idx2] == bid) height++;
                    else break;
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
                buildQuadPosX(
                    startY, startZ, height, depth, x,
                    offsetX, offsetY, offsetZ, bid,
                    outVertices, outIndices
                );
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

                int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x - 1, y, z, manager);
                bool exposed = (neighborID != id);
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
                    if (mask[idx2] == bid) height++;
                    else break;
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
                buildQuadNegX(
                    startY, startZ, height, depth, x,
                    offsetX, offsetY, offsetZ, bid,
                    outVertices, outIndices
                );
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

                int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x, y + 1, z, manager);
                bool exposed = (neighborID != id);
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
                    if (mask[idx2] == bid) width++;
                    else break;
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
                buildQuadPosY(
                    startX, startZ, width, depth, y,
                    offsetX, offsetY, offsetZ, bid,
                    outVertices, outIndices
                );
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

                int neighborID = getBlockIDGlobal(chunk, cx, cy, cz, x, y - 1, z, manager);
                bool exposed = (neighborID != id);
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
                    if (mask[idx2] == bid) width++;
                    else break;
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
                buildQuadNegY(
                    startX, startZ, width, depth, y,
                    offsetX, offsetY, offsetZ, bid,
                    outVertices, outIndices
                );
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

    // Debug
    std::cout << "[Mesh Debug] Chunk(" << cx << "," << cy << "," << cz
        << ") => " << outVertices.size() << " verts, "
        << outIndices.size() << " inds\n";
}

/**
 * Builds a mesh from a generic in-memory voxel array of size dsX * dsY * dsZ.
 * If useGreedy == true, adapt the same boundary logic inside that code path.
 * For now, we skip boundary adjacency for LOD arrays, or treat them as “no neighbor.”
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

    // Basic naive approach ignoring chunk neighbors at LOD:
    if (!useGreedy)
    {
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

                    // Helper for local adjacency
                    auto getLocalID = [&](int xx, int yy, int zz)
                        {
                            if (xx < 0 || xx >= dsX ||
                                yy < 0 || yy >= dsY ||
                                zz < 0 || zz >= dsZ)
                            {
                                return -1; // out of range => treat as air
                            }
                            return voxelArray[xx + dsX * (yy + dsY * zz)];
                        };

                    // +X
                    if (getLocalID(x + 1, y, z) != voxelID)
                    {
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
                    if (getLocalID(x - 1, y, z) != voxelID)
                    {
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
                    if (getLocalID(x, y + 1, z) != voxelID)
                    {
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
                    if (getLocalID(x, y - 1, z) != voxelID)
                    {
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
                    if (getLocalID(x, y, z + 1) != voxelID)
                    {
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
                    if (getLocalID(x, y, z - 1) != voxelID)
                    {
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
        // We'll define a small lambda to get the voxel ID or -1 if out-of-bounds
        auto getLocalID = [&](int xx, int yy, int zz) -> int {
            if (xx < 0 || xx >= dsX ||
                yy < 0 || yy >= dsY ||
                zz < 0 || zz >= dsZ)
            {
                return -1; // treat out-of-bounds as empty
            }
            return voxelArray[xx + dsX * (yy + dsY * zz)];
            };

        // For LOD or small arrays, you could adapt your "greedy" approach here if you like.
        // For simplicity, we skip neighbor merges in LOD arrays. So here's a brief sample:

        // +Z direction
        for (int z = 0; z < dsZ; z++)
        {
            std::vector<int> mask(dsX * dsY, -1);
            for (int y = 0; y < dsY; y++)
            {
                for (int x = 0; x < dsX; x++)
                {
                    int id = getLocalID(x, y, z);
                    if (id <= 0) continue;

                    int neighborID = getLocalID(x, y, z + 1);
                    bool exposed = (neighborID != id);
                    if (exposed) {
                        size_t idx = static_cast<size_t>(y) * dsX + x;
                        mask[idx] = id;
                    }
                }
            }
            for (int row = 0; row < dsY; row++)
            {
                int col = 0;
                while (col < dsX)
                {
                    size_t idx = static_cast<size_t>(row) * dsX + col;
                    int bid = mask[idx];
                    if (bid < 0) {
                        col++;
                        continue;
                    }
                    int width = 1;
                    while ((col + width) < dsX)
                    {
                        size_t idx2 = static_cast<size_t>(row) * dsX + (col + width);
                        if (mask[idx2] == bid) width++;
                        else break;
                    }
                    int height = 1;
                    bool done = false;
                    while (!done)
                    {
                        int nextRow = row + height;
                        if (nextRow >= dsY) break;
                        for (int c2 = 0; c2 < width; c2++)
                        {
                            size_t idx3 = static_cast<size_t>(nextRow) * dsX + (col + c2);
                            if (mask[idx3] != bid)
                            {
                                done = true;
                                break;
                            }
                        }
                        if (!done) height++;
                    }

                    float zPos = float(z + 1 + worldOffsetZ);
                    float X0 = float(col + worldOffsetX);
                    float Y0 = float(row + worldOffsetY);
                    float X1 = float(col + width + worldOffsetX);
                    float Y1 = float(row + height + worldOffsetY);

                    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(bid);
                    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

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

                    for (int rr = 0; rr < height; rr++)
                    {
                        for (int cc = 0; cc < width; cc++)
                        {
                            size_t idx4 = static_cast<size_t>(row + rr) * dsX + (col + cc);
                            mask[idx4] = -1;
                        }
                    }
                    col += width;
                }
            }
        }

        // -Z direction
        for (int z = 0; z < dsZ; z++)
        {
            std::vector<int> mask(dsX * dsY, -1);
            for (int y = 0; y < dsY; y++)
            {
                for (int x = 0; x < dsX; x++)
                {
                    int id = getLocalID(x, y, z);
                    if (id <= 0) continue;

                    int neighborID = getLocalID(x, y, z - 1);
                    bool exposed = (neighborID != id);
                    if (exposed) {
                        size_t idx = static_cast<size_t>(y) * dsX + x;
                        mask[idx] = id;
                    }
                }
            }
            for (int row = 0; row < dsY; row++)
            {
                int col = 0;
                while (col < dsX)
                {
                    size_t idx = static_cast<size_t>(row) * dsX + col;
                    int bid = mask[idx];
                    if (bid < 0) {
                        col++;
                        continue;
                    }
                    int width = 1;
                    while ((col + width) < dsX)
                    {
                        size_t idx2 = static_cast<size_t>(row) * dsX + (col + width);
                        if (mask[idx2] == bid) width++;
                        else break;
                    }
                    int height = 1;
                    bool done = false;
                    while (!done)
                    {
                        int nr = row + height;
                        if (nr >= dsY) break;
                        for (int c2 = 0; c2 < width; c2++)
                        {
                            size_t idx3 = static_cast<size_t>(nr) * dsX + (col + c2);
                            if (mask[idx3] != bid)
                            {
                                done = true;
                                break;
                            }
                        }
                        if (!done) height++;
                    }

                    float zPos = float(z + worldOffsetZ);
                    float X0 = float(col + worldOffsetX);
                    float Y0 = float(row + worldOffsetY);
                    float X1 = float(col + width + worldOffsetX);
                    float Y1 = float(row + height + worldOffsetY);

                    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(bid);
                    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

                    int startIndex = (int)outVertices.size();
                    // -Z => normal points negative Z, so wind in reversed X order
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

                    for (int rr = 0; rr < height; rr++)
                    {
                        for (int cc = 0; cc < width; cc++)
                        {
                            size_t idx4 = static_cast<size_t>(row + rr) * dsX + (col + cc);
                            mask[idx4] = -1;
                        }
                    }
                    col += width;
                }
            }
        }

        // +X direction
        for (int x = 0; x < dsX; x++)
        {
            std::vector<int> mask(dsY * dsZ, -1);
            for (int z = 0; z < dsZ; z++)
            {
                for (int y = 0; y < dsY; y++)
                {
                    int id = getLocalID(x, y, z);
                    if (id <= 0) continue;

                    int neighborID = getLocalID(x + 1, y, z);
                    bool exposed = (neighborID != id);
                    if (exposed) {
                        size_t idx = static_cast<size_t>(z) * dsY + y;
                        mask[idx] = id;
                    }
                }
            }
            for (int startZ = 0; startZ < dsZ; startZ++)
            {
                int startY = 0;
                while (startY < dsY)
                {
                    size_t idx = static_cast<size_t>(startZ) * dsY + startY;
                    int bid = mask[idx];
                    if (bid < 0) {
                        startY++;
                        continue;
                    }
                    int height = 1;
                    while ((startY + height) < dsY)
                    {
                        size_t idx2 = static_cast<size_t>(startZ) * dsY + (startY + height);
                        if (mask[idx2] == bid) height++;
                        else break;
                    }
                    int depth = 1;
                    bool done = false;
                    while (!done)
                    {
                        int nz = startZ + depth;
                        if (nz >= dsZ) break;
                        for (int hy = 0; hy < height; hy++)
                        {
                            size_t idx3 = static_cast<size_t>(nz) * dsY + (startY + hy);
                            if (mask[idx3] != bid)
                            {
                                done = true;
                                break;
                            }
                        }
                        if (!done) depth++;
                    }

                    float xPos = float((x + 1) + worldOffsetX);
                    float Y0 = float(startY + worldOffsetY);
                    float Z0 = float(startZ + worldOffsetZ);
                    float Y1 = float(startY + height + worldOffsetY);
                    float Z1 = float(startZ + depth + worldOffsetZ);

                    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(bid);
                    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

                    int startIndex = (int)outVertices.size();
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

                    for (int dz = 0; dz < depth; dz++)
                    {
                        for (int hy = 0; hy < height; hy++)
                        {
                            size_t idx4 = static_cast<size_t>(startZ + dz) * dsY + (startY + hy);
                            mask[idx4] = -1;
                        }
                    }
                    startY += height;
                }
            }
        }

        // -X direction
        for (int x = 0; x < dsX; x++)
        {
            std::vector<int> mask(dsY * dsZ, -1);
            for (int z = 0; z < dsZ; z++)
            {
                for (int y = 0; y < dsY; y++)
                {
                    int id = getLocalID(x, y, z);
                    if (id <= 0) continue;

                    int neighborID = getLocalID(x - 1, y, z);
                    bool exposed = (neighborID != id);
                    if (exposed) {
                        size_t idx = static_cast<size_t>(z) * dsY + y;
                        mask[idx] = id;
                    }
                }
            }
            for (int startZ = 0; startZ < dsZ; startZ++)
            {
                int startY = 0;
                while (startY < dsY)
                {
                    size_t idx = static_cast<size_t>(startZ) * dsY + startY;
                    int bid = mask[idx];
                    if (bid < 0) {
                        startY++;
                        continue;
                    }
                    int height = 1;
                    while ((startY + height) < dsY)
                    {
                        size_t idx2 = static_cast<size_t>(startZ) * dsY + (startY + height);
                        if (mask[idx2] == bid) height++;
                        else break;
                    }
                    int depth = 1;
                    bool done = false;
                    while (!done)
                    {
                        int nz = startZ + depth;
                        if (nz >= dsZ) break;
                        for (int hy = 0; hy < height; hy++)
                        {
                            size_t idx3 = static_cast<size_t>(nz) * dsY + (startY + hy);
                            if (mask[idx3] != bid)
                            {
                                done = true;
                                break;
                            }
                        }
                        if (!done) depth++;
                    }

                    float xPos = float(x + worldOffsetX);
                    float Y0 = float(startY + worldOffsetY);
                    float Z0 = float(startZ + worldOffsetZ);
                    float Y1 = float(startY + height + worldOffsetY);
                    float Z1 = float(startZ + depth + worldOffsetZ);

                    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(bid);
                    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

                    int startIndex = (int)outVertices.size();
                    // -X => normal in negative X
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

                    for (int dz = 0; dz < depth; dz++)
                    {
                        for (int hy = 0; hy < height; hy++)
                        {
                            size_t idx4 = static_cast<size_t>(startZ + dz) * dsY + (startY + hy);
                            mask[idx4] = -1;
                        }
                    }
                    startY += height;
                }
            }
        }

        // +Y direction
        for (int y = 0; y < dsY; y++)
        {
            std::vector<int> mask(dsX * dsZ, -1);
            for (int z = 0; z < dsZ; z++)
            {
                for (int x = 0; x < dsX; x++)
                {
                    int id = getLocalID(x, y, z);
                    if (id <= 0) continue;

                    int neighborID = getLocalID(x, y + 1, z);
                    bool exposed = (neighborID != id);
                    if (exposed) {
                        size_t idx = static_cast<size_t>(z) * dsX + x;
                        mask[idx] = id;
                    }
                }
            }
            for (int startZ = 0; startZ < dsZ; startZ++)
            {
                int startX = 0;
                while (startX < dsX)
                {
                    size_t idx = static_cast<size_t>(startZ) * dsX + startX;
                    int bid = mask[idx];
                    if (bid < 0) {
                        startX++;
                        continue;
                    }
                    int width = 1;
                    while ((startX + width) < dsX)
                    {
                        size_t idx2 = static_cast<size_t>(startZ) * dsX + (startX + width);
                        if (mask[idx2] == bid) width++;
                        else break;
                    }
                    int depth = 1;
                    bool done = false;
                    while (!done)
                    {
                        int nz = startZ + depth;
                        if (nz >= dsZ) break;
                        for (int wx = 0; wx < width; wx++)
                        {
                            size_t idx3 = static_cast<size_t>(nz) * dsX + (startX + wx);
                            if (mask[idx3] != bid)
                            {
                                done = true;
                                break;
                            }
                        }
                        if (!done) depth++;
                    }

                    float yPos = float((y + 1) + worldOffsetY);
                    float X0 = float(startX + worldOffsetX);
                    float Z0 = float(startZ + worldOffsetZ);
                    float X1 = float(startX + width + worldOffsetX);
                    float Z1 = float(startZ + depth + worldOffsetZ);

                    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(bid);
                    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

                    int startIndex = (int)outVertices.size();
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

                    for (int dz = 0; dz < depth; dz++)
                    {
                        for (int wx = 0; wx < width; wx++)
                        {
                            size_t idx4 = static_cast<size_t>(startZ + dz) * dsX + (startX + wx);
                            mask[idx4] = -1;
                        }
                    }
                    startX += width;
                }
            }
        }

        // -Y direction
        for (int y = 0; y < dsY; y++)
        {
            std::vector<int> mask(dsX * dsZ, -1);
            for (int z = 0; z < dsZ; z++)
            {
                for (int x = 0; x < dsX; x++)
                {
                    int id = getLocalID(x, y, z);
                    if (id <= 0) continue;

                    int neighborID = getLocalID(x, y - 1, z);
                    bool exposed = (neighborID != id);
                    if (exposed) {
                        size_t idx = static_cast<size_t>(z) * dsX + x;
                        mask[idx] = id;
                    }
                }
            }
            for (int startZ = 0; startZ < dsZ; startZ++)
            {
                int startX = 0;
                while (startX < dsX)
                {
                    size_t idx = static_cast<size_t>(startZ) * dsX + startX;
                    int bid = mask[idx];
                    if (bid < 0) {
                        startX++;
                        continue;
                    }
                    int width = 1;
                    while ((startX + width) < dsX)
                    {
                        size_t idx2 = static_cast<size_t>(startZ) * dsX + (startX + width);
                        if (mask[idx2] == bid) width++;
                        else break;
                    }
                    int depth = 1;
                    bool done = false;
                    while (!done)
                    {
                        int nz = startZ + depth;
                        if (nz >= dsZ) break;
                        for (int wx = 0; wx < width; wx++)
                        {
                            size_t idx3 = static_cast<size_t>(nz) * dsX + (startX + wx);
                            if (mask[idx3] != bid)
                            {
                                done = true;
                                break;
                            }
                        }
                        if (!done) depth++;
                    }

                    float yPos = float(y + worldOffsetY);
                    float X0 = float(startX + worldOffsetX);
                    float Z0 = float(startZ + worldOffsetZ);
                    float X1 = float(startX + width + worldOffsetX);
                    float Z1 = float(startZ + depth + worldOffsetZ);

                    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(bid);
                    float r = vt.color.r, g = vt.color.g, b = vt.color.b;

                    int startIndex = (int)outVertices.size();
                    // -Y => normal in negative Y
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

                    for (int dz = 0; dz < depth; dz++)
                    {
                        for (int wx = 0; wx < width; wx++)
                        {
                            size_t idx4 = static_cast<size_t>(startZ + dz) * dsX + (startX + wx);
                            mask[idx4] = -1;
                        }
                    }
                    startX += width;
                }
            }
        }
    }
}

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
