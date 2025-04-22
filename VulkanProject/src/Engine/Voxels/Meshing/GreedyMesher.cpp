// ============================================================================
// GreedyMesher.cpp  – original builders + merge?cap
// ============================================================================

#include "GreedyMesher.h"
#include "Engine/Voxels/IBlockProvider.h"
#include "../VoxelTypeRegistry.h"
#include "../VoxelType.h"
#include "../Chunk.h"
#include "../ChunkManager.h"

#include <vector>
#include <cstddef>
#include <algorithm>

// ---------------------------------------------------------------------------
// Tunable: maximum merge run (per axis) to avoid huge quads
// ---------------------------------------------------------------------------
static constexpr int MAX_GREEDY_RUN = 32;

// ---------------------------------------------------------------------------
// packColor: clamp [0..1] ? R8G8B8A8
// ---------------------------------------------------------------------------
static uint32_t packColor(float r, float g, float b)
{
    r = std::max(0.f, std::min(1.f, r));
    g = std::max(0.f, std::min(1.f, g));
    b = std::max(0.f, std::min(1.f, b));

    uint32_t R = static_cast<uint32_t>(r * 255.0f);
    uint32_t G = static_cast<uint32_t>(g * 255.0f);
    uint32_t B = static_cast<uint32_t>(b * 255.0f);
    return (255u << 24) | (B << 16) | (G << 8) | R;
}

// ---------------------------------------------------------------------------
// Quad helpers — identical to your original code (only indices via push_back)
// ---------------------------------------------------------------------------
static void buildQuadPosZ(int sx, int sy, int w, int h, int z,
    int ox, int oy, int oz, int id,
    std::vector<Vertex>& V, std::vector<uint32_t>& I)
{
    const auto& vt = VoxelTypeRegistry::get().getVoxel(id);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float Z = float(z + 1 + oz);
    float X0 = float(sx + ox), Y0 = float(sy + oy);
    float X1 = float(sx + w + ox), Y1 = float(sy + h + oy);

    uint32_t base = static_cast<uint32_t>(V.size());
    V.emplace_back(X0, Y0, Z, col); V.emplace_back(X1, Y0, Z, col);
    V.emplace_back(X1, Y1, Z, col); V.emplace_back(X0, Y1, Z, col);

    I.push_back(base); I.push_back(base + 1); I.push_back(base + 2);
    I.push_back(base + 2); I.push_back(base + 3); I.push_back(base);
}

static void buildQuadNegZ(int sx, int sy, int w, int h, int z,
    int ox, int oy, int oz, int id,
    std::vector<Vertex>& V, std::vector<uint32_t>& I)
{
    const auto& vt = VoxelTypeRegistry::get().getVoxel(id);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float Z = float(z + oz);
    float X0 = float(sx + ox), Y0 = float(sy + oy);
    float X1 = float(sx + w + ox), Y1 = float(sy + h + oy);

    uint32_t base = static_cast<uint32_t>(V.size());
    V.emplace_back(X1, Y0, Z, col); V.emplace_back(X0, Y0, Z, col);
    V.emplace_back(X0, Y1, Z, col); V.emplace_back(X1, Y1, Z, col);

    I.push_back(base); I.push_back(base + 1); I.push_back(base + 2);
    I.push_back(base + 2); I.push_back(base + 3); I.push_back(base);
}

static void buildQuadPosX(int sy, int sz, int h, int d, int x,
    int ox, int oy, int oz, int id,
    std::vector<Vertex>& V, std::vector<uint32_t>& I)
{
    const auto& vt = VoxelTypeRegistry::get().getVoxel(id);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float X = float(x + 1 + ox);
    float Y0 = float(sy + oy), Z0 = float(sz + oz);
    float Y1 = float(sy + h + oy), Z1 = float(sz + d + oz);

    uint32_t base = static_cast<uint32_t>(V.size());
    V.emplace_back(X, Y0, Z0, col); V.emplace_back(X, Y0, Z1, col);
    V.emplace_back(X, Y1, Z1, col); V.emplace_back(X, Y1, Z0, col);

    I.push_back(base); I.push_back(base + 1); I.push_back(base + 2);
    I.push_back(base + 2); I.push_back(base + 3); I.push_back(base);
}

static void buildQuadNegX(int sy, int sz, int h, int d, int x,
    int ox, int oy, int oz, int id,
    std::vector<Vertex>& V, std::vector<uint32_t>& I)
{
    const auto& vt = VoxelTypeRegistry::get().getVoxel(id);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float X = float(x + ox);
    float Y0 = float(sy + oy), Z0 = float(sz + oz);
    float Y1 = float(sy + h + oy), Z1 = float(sz + d + oz);

    uint32_t base = static_cast<uint32_t>(V.size());
    V.emplace_back(X, Y0, Z1, col); V.emplace_back(X, Y0, Z0, col);
    V.emplace_back(X, Y1, Z0, col); V.emplace_back(X, Y1, Z1, col);

    I.push_back(base); I.push_back(base + 1); I.push_back(base + 2);
    I.push_back(base + 2); I.push_back(base + 3); I.push_back(base);
}

static void buildQuadPosY(int sx, int sz, int w, int d, int y,
    int ox, int oy, int oz, int id,
    std::vector<Vertex>& V, std::vector<uint32_t>& I)
{
    const auto& vt = VoxelTypeRegistry::get().getVoxel(id);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float Y = float(y + 1 + oy);
    float X0 = float(sx + ox), Z0 = float(sz + oz);
    float X1 = float(sx + w + ox), Z1 = float(sz + d + oz);

    uint32_t base = static_cast<uint32_t>(V.size());
    V.emplace_back(X0, Y, Z0, col); V.emplace_back(X1, Y, Z0, col);
    V.emplace_back(X1, Y, Z1, col); V.emplace_back(X0, Y, Z1, col);

    I.push_back(base); I.push_back(base + 1); I.push_back(base + 2);
    I.push_back(base + 2); I.push_back(base + 3); I.push_back(base);
}

static void buildQuadNegY(int sx, int sz, int w, int d, int y,
    int ox, int oy, int oz, int id,
    std::vector<Vertex>& V, std::vector<uint32_t>& I)
{
    const auto& vt = VoxelTypeRegistry::get().getVoxel(id);
    uint32_t col = packColor(vt.color.r, vt.color.g, vt.color.b);

    float Y = float(y + oy);
    float X0 = float(sx + ox), Z0 = float(sz + oz);
    float X1 = float(sx + w + ox), Z1 = float(sz + d + oz);

    uint32_t base = static_cast<uint32_t>(V.size());
    V.emplace_back(X1, Y, Z0, col); V.emplace_back(X0, Y, Z0, col);
    V.emplace_back(X0, Y, Z1, col); V.emplace_back(X1, Y, Z1, col);

    I.push_back(base); I.push_back(base + 1); I.push_back(base + 2);
    I.push_back(base + 2); I.push_back(base + 3); I.push_back(base);
}

// ---------------------------------------------------------------------------
// Thin wrapper for Chunk
// ---------------------------------------------------------------------------
bool GreedyMesher::generateMesh(
    Chunk& c, int cx, int cy, int cz,
    std::vector<Vertex>& V, std::vector<uint32_t>& I,
    int ox, int oy, int oz, const ChunkManager& M) const
{
    return generateMesh(static_cast<const IBlockProvider&>(c),
        cx, cy, cz, V, I, ox, oy, oz, M);
}

// ---------------------------------------------------------------------------
// Core generateMesh with MAX_GREEDY_RUN limiter (no C++17 syntax)
// ---------------------------------------------------------------------------
bool GreedyMesher::generateMesh(
    const IBlockProvider& blk,
    int cx, int cy, int cz,
    std::vector<Vertex>& V, std::vector<uint32_t>& I,
    int ox, int oy, int oz,
    const ChunkManager& mgr) const
{
    V.clear(); I.clear();
    V.reserve(4096); I.reserve(6144);

    const int SX = blk.getSizeX(), SY = blk.getSizeY(), SZ = blk.getSizeZ();

    auto isSolidID = [&](int id)
        { return id > 0 && VoxelTypeRegistry::get().getVoxel(id).isSolid; };

    auto divFloor = [](int v, int d)
        { return (v >= 0) ? v / d : (v - (d - 1)) / d; };

    /* -------------------------------------------------------------------- */
    /* neighbour test protected by shared_ptr                               */
    /* -------------------------------------------------------------------- */
    auto isSolidGlobal = [&](int x, int y, int z) -> bool
        {
            // inside current chunk
            if (x >= 0 && x < SX && y >= 0 && y < SY && z >= 0 && z < SZ)
                return isSolidID(blk.getBlock(x, y, z));

            // convert to world coords
            int wx = cx * Chunk::SIZE_X + x;
            int wy = cy * Chunk::SIZE_Y + y;
            int wz = cz * Chunk::SIZE_Z + z;

            int ncx = divFloor(wx, Chunk::SIZE_X);
            int ncy = divFloor(wy, Chunk::SIZE_Y);
            int ncz = divFloor(wz, Chunk::SIZE_Z);

            if (!mgr.hasChunk(ncx, ncy, ncz))
                return false;                    // neighbour not loaded → treat as air

            std::shared_ptr<Chunk> neigh = mgr.getChunk(ncx, ncy, ncz);
            if (!neigh) return false;

            return isSolidID(neigh->getBlock(wx - ncx * Chunk::SIZE_X,
                wy - ncy * Chunk::SIZE_Y,
                wz - ncz * Chunk::SIZE_Z));
        };

    auto get = [&](int x, int y, int z) { return blk.getBlock(x, y, z); };

    // ---------------- +Z ----------------
    for (int z = 0; z < SZ; ++z)
    {
        std::vector<int> mask(SX * SY, -1);
        for (int y = 0; y < SY; ++y) for (int x = 0; x < SX; ++x)
        {
            int id = get(x, y, z);
            if (id > 0 && !isSolidGlobal(x, y, z + 1))
                mask[y * SX + x] = id;
        }

        for (int row = 0; row < SY; ++row)
        {
            int col = 0;
            while (col < SX)
            {
                int id = mask[row * SX + col];
                if (id < 0) { ++col; continue; }

                int w = 1;
                while (w < MAX_GREEDY_RUN && col + w < SX && mask[row * SX + col + w] == id) ++w;

                int h = 1; bool stop = false;
                while (!stop && h < MAX_GREEDY_RUN && row + h < SY)
                {
                    for (int k = 0; k < w; ++k)
                        if (mask[(row + h) * SX + col + k] != id) { stop = true; break; }
                    if (!stop) ++h;
                }

                buildQuadPosZ(col, row, w, h, z, ox, oy, oz, id, V, I);
                for (int dy = 0; dy < h; ++dy) for (int dx = 0; dx < w; ++dx)
                    mask[(row + dy) * SX + col + dx] = -1;
                col += w;
            }
        }
    }

    // ---------------- -Z ----------------
    for (int z = 0; z < SZ; ++z)
    {
        std::vector<int> mask(SX * SY, -1);
        for (int y = 0; y < SY; ++y) for (int x = 0; x < SX; ++x)
        {
            int id = get(x, y, z);
            if (id > 0 && !isSolidGlobal(x, y, z - 1))
                mask[y * SX + x] = id;
        }

        for (int row = 0; row < SY; ++row)
        {
            int col = 0;
            while (col < SX)
            {
                int id = mask[row * SX + col];
                if (id < 0) { ++col; continue; }

                int w = 1;
                while (w < MAX_GREEDY_RUN && col + w < SX && mask[row * SX + col + w] == id) ++w;

                int h = 1; bool stop = false;
                while (!stop && h < MAX_GREEDY_RUN && row + h < SY)
                {
                    for (int k = 0; k < w; ++k)
                        if (mask[(row + h) * SX + col + k] != id) { stop = true; break; }
                    if (!stop) ++h;
                }

                buildQuadNegZ(col, row, w, h, z, ox, oy, oz, id, V, I);
                for (int dy = 0; dy < h; ++dy) for (int dx = 0; dx < w; ++dx)
                    mask[(row + dy) * SX + col + dx] = -1;
                col += w;
            }
        }
    }

    // ---------------- +X ----------------
    for (int x = 0; x < SX; ++x)
    {
        std::vector<int> mask(SY * SZ, -1);
        for (int z = 0; z < SZ; ++z) for (int y = 0; y < SY; ++y)
        {
            int id = get(x, y, z);
            if (id > 0 && !isSolidGlobal(x + 1, y, z))
                mask[z * SY + y] = id;
        }

        for (int row = 0; row < SZ; ++row)
        {
            int col = 0;
            while (col < SY)
            {
                int id = mask[row * SY + col];
                if (id < 0) { ++col; continue; }

                int h = 1;
                while (h < MAX_GREEDY_RUN && col + h < SY && mask[row * SY + col + h] == id) ++h;

                int d = 1; bool stop = false;
                while (!stop && d < MAX_GREEDY_RUN && row + d < SZ)
                {
                    for (int k = 0; k < h; ++k)
                        if (mask[(row + d) * SY + col + k] != id) { stop = true; break; }
                    if (!stop) ++d;
                }

                buildQuadPosX(col, row, h, d, x, ox, oy, oz, id, V, I);
                for (int dz = 0; dz < d; ++dz) for (int dy = 0; dy < h; ++dy)
                    mask[(row + dz) * SY + col + dy] = -1;
                col += h;
            }
        }
    }

    // ---------------- -X ----------------
    for (int x = 0; x < SX; ++x)
    {
        std::vector<int> mask(SY * SZ, -1);
        for (int z = 0; z < SZ; ++z) for (int y = 0; y < SY; ++y)
        {
            int id = get(x, y, z);
            if (id > 0 && !isSolidGlobal(x - 1, y, z))
                mask[z * SY + y] = id;
        }

        for (int row = 0; row < SZ; ++row)
        {
            int col = 0;
            while (col < SY)
            {
                int id = mask[row * SY + col];
                if (id < 0) { ++col; continue; }

                int h = 1;
                while (h < MAX_GREEDY_RUN && col + h < SY && mask[row * SY + col + h] == id) ++h;

                int d = 1; bool stop = false;
                while (!stop && d < MAX_GREEDY_RUN && row + d < SZ)
                {
                    for (int k = 0; k < h; ++k)
                        if (mask[(row + d) * SY + col + k] != id) { stop = true; break; }
                    if (!stop) ++d;
                }

                buildQuadNegX(col, row, h, d, x, ox, oy, oz, id, V, I);
                for (int dz = 0; dz < d; ++dz)
                    for (int dy = 0; dy < h; ++dy)
                        mask[(row + dz) * SY + col + dy] = -1;

                col += h;
            }
        }
    }

    // ---------------- +Y ----------------
    for (int y = 0; y < SY; ++y)
    {
        std::vector<int> mask(SX * SZ, -1);
        for (int z = 0; z < SZ; ++z)
            for (int x = 0; x < SX; ++x)
            {
                int id = get(x, y, z);
                if (id > 0 && !isSolidGlobal(x, y + 1, z))
                    mask[z * SX + x] = id;
            }

        for (int row = 0; row < SZ; ++row)
        {
            int col = 0;
            while (col < SX)
            {
                int id = mask[row * SX + col];
                if (id < 0) { ++col; continue; }

                int w = 1;
                while (w < MAX_GREEDY_RUN && col + w < SX &&
                    mask[row * SX + col + w] == id) ++w;

                int d = 1; bool stop = false;
                while (!stop && d < MAX_GREEDY_RUN && row + d < SZ)
                {
                    for (int k = 0; k < w; ++k)
                        if (mask[(row + d) * SX + col + k] != id)
                        {
                            stop = true; break;
                        }
                    if (!stop) ++d;
                }

                buildQuadPosY(col, row, w, d, y, ox, oy, oz, id, V, I);

                for (int dz = 0; dz < d; ++dz)
                    for (int dx = 0; dx < w; ++dx)
                        mask[(row + dz) * SX + col + dx] = -1;

                col += w;
            }
        }
    }

    // ---------------- -Y ----------------
    for (int y = 0; y < SY; ++y)
    {
        std::vector<int> mask(SX * SZ, -1);
        for (int z = 0; z < SZ; ++z)
            for (int x = 0; x < SX; ++x)
            {
                int id = get(x, y, z);
                if (id > 0 && !isSolidGlobal(x, y - 1, z))
                    mask[z * SX + x] = id;
            }

        for (int row = 0; row < SZ; ++row)
        {
            int col = 0;
            while (col < SX)
            {
                int id = mask[row * SX + col];
                if (id < 0) { ++col; continue; }

                int w = 1;
                while (w < MAX_GREEDY_RUN && col + w < SX &&
                    mask[row * SX + col + w] == id) ++w;

                int d = 1; bool stop = false;
                while (!stop && d < MAX_GREEDY_RUN && row + d < SZ)
                {
                    for (int k = 0; k < w; ++k)
                        if (mask[(row + d) * SX + col + k] != id)
                        {
                            stop = true; break;
                        }
                    if (!stop) ++d;
                }

                buildQuadNegY(col, row, w, d, y, ox, oy, oz, id, V, I);

                for (int dz = 0; dz < d; ++dz)
                    for (int dx = 0; dx < w; ++dx)
                        mask[(row + dz) * SX + col + dx] = -1;

                col += w;
            }
        }
    }

    // ------------------------------------------------------------------------
    // Return true if any geometry was produced
    // ------------------------------------------------------------------------
    return !V.empty();
}
