// LODMesher.cpp

#include "LODMesher.h"
#include "Engine/Voxels/Chunk.h"
#include "Engine/Voxels/ChunkManager.h"
#include "Engine/Voxels/Meshing/GreedyMesher.h"  // or GreedyMesher if you prefer
#include <algorithm>  // For std::min, std::max, etc.
#include <Engine/Voxels/VoxelTypeRegistry.h>

// --------------------------------------------------------
// buildAllLODs
// --------------------------------------------------------
MultiLODResult LODMesher::buildAllLODs(
    Chunk& chunk,
    int cx, int cy, int cz,
    const IMesher* baseMesher,
    const ChunkManager& manager)
{
    MultiLODResult result;
    result.chunkPtr = &chunk;
    result.cx = cx;
    result.cy = cy;
    result.cz = cz;

    // 1) LOD0 => the real mesher
    {
        std::vector<Vertex> verts;
        std::vector<uint32_t> inds;

        int offX = cx * Chunk::SIZE_X;
        int offY = cy * Chunk::SIZE_Y;
        int offZ = cz * Chunk::SIZE_Z;

        baseMesher->generateMesh(
            chunk,
            cx, cy, cz,
            verts, inds,
            offX, offY, offZ,
            manager
        );

        result.lods[0].verts = std::move(verts);
        result.lods[0].inds = std::move(inds);
    }

    // 2) For LOD1..N => downsample + mesh
    //    In this example, we set up to result.MAX_LODS - 1
    for (int lodLevel = 1; lodLevel < MultiLODResult::MAX_LODS; lodLevel++)
    {
        std::vector<int> coarseVoxels;
        downsampleChunkData(chunk, (1 << lodLevel), coarseVoxels);

        // The coarser dimensions
        int cSizeX = Chunk::SIZE_X / (1 << lodLevel);
        int cSizeY = Chunk::SIZE_Y / (1 << lodLevel);
        int cSizeZ = Chunk::SIZE_Z / (1 << lodLevel);

        std::vector<Vertex> verts;
        std::vector<uint32_t> inds;

        int offX = cx * Chunk::SIZE_X;
        int offY = cy * Chunk::SIZE_Y;
        int offZ = cz * Chunk::SIZE_Z;

        meshDownsampledData(
            coarseVoxels, cSizeX, cSizeY, cSizeZ,
            offX, offY, offZ,
            verts, inds
        );

        result.lods[lodLevel].verts = std::move(verts);
        result.lods[lodLevel].inds = std::move(inds);
    }

    return result;
}

// --------------------------------------------------------
// downsampleChunkData
// --------------------------------------------------------
void LODMesher::downsampleChunkData(
    const Chunk& src,
    int factor,
    std::vector<int>& outVoxels)
{
    // factor=2 means each 2x2x2 block in the chunk becomes 1 voxel in the result
    // chunk dimension => e.g. 16 x 128 x 16
    // cSizeX => 16/2=8, cSizeY=>128/2=64, cSizeZ=>16/2=8
    const int cSizeX = Chunk::SIZE_X / factor;
    const int cSizeY = Chunk::SIZE_Y / factor;
    const int cSizeZ = Chunk::SIZE_Z / factor;

    outVoxels.resize(cSizeX * cSizeY * cSizeZ, 0);

    for (int zz = 0; zz < cSizeZ; zz++)
    {
        for (int yy = 0; yy < cSizeY; yy++)
        {
            for (int xx = 0; xx < cSizeX; xx++)
            {
                // The NxNxN region in the original chunk
                int startX = xx * factor;
                int startY = yy * factor;
                int startZ = zz * factor;

                // For this example, we pick the first non-air block we find:
                // (You could also do majority, or an average, or topmost.)
                int chosenID = 0; // 0 => air
                bool foundSolid = false;

                for (int dz = 0; dz < factor && !foundSolid; dz++)
                {
                    for (int dy = 0; dy < factor && !foundSolid; dy++)
                    {
                        for (int dx = 0; dx < factor; dx++)
                        {
                            int realX = startX + dx;
                            int realY = startY + dy;
                            int realZ = startZ + dz;

                            int id = src.getBlock(realX, realY, realZ);
                            if (id != 0) // if not air
                            {
                                chosenID = id;
                                foundSolid = true;
                                break;
                            }
                        }
                    }
                }

                size_t outIndex = (zz * cSizeY + yy) * cSizeX + xx;
                outVoxels[outIndex] = chosenID;
            }
        }
    }
}

// --------------------------------------------------------
// meshDownsampledData
// --------------------------------------------------------
void LODMesher::meshDownsampledData(
    const std::vector<int>& coarseVoxels,
    int coarseSizeX,
    int coarseSizeY,
    int coarseSizeZ,
    int offsetX,
    int offsetY,
    int offsetZ,
    std::vector<Vertex>& outVerts,
    std::vector<uint32_t>& outIndices)
{
    outVerts.clear();
    outIndices.clear();

    // -------------------------------------------------------
    // 1) Create a "MiniChunk" that holds the downsampled data
    //    It overrides getBlock() so GreedyMesher can read voxel IDs.
    // -------------------------------------------------------
    class MiniChunk : public Chunk
    {
    public:
        MiniChunk(int sx, int sy, int sz,
            int ox, int oy, int oz,
            const std::vector<int>& dataRef)
            : Chunk(0, 0, 0)  // We'll pass 0,0,0 for the chunk coords; it's not used here
            , m_sizeX(sx), m_sizeY(sy), m_sizeZ(sz)
            , m_offsetX(ox), m_offsetY(oy), m_offsetZ(oz)
            , m_data(dataRef)
        {
            // We do NOT allocate the usual chunk blocks.
            // Our "m_data" is an external reference to coarseVoxels.
        }

        // Override getBlock so GreedyMesher can fetch voxel IDs
        virtual int getBlock(int x, int y, int z) const override
        {
            // Out-of-range => treat as air
            if (x < 0 || x >= m_sizeX ||
                y < 0 || y >= m_sizeY ||
                z < 0 || z >= m_sizeZ)
            {
                return 0;
            }
            // Index into our coarse data
            int idx = (z * m_sizeY + y) * m_sizeX + x;
            return m_data[idx];
        }

        // We also override the chunk's bounding box or any other calls if needed,
        // but typically just getBlock() is enough for the mesher.
        // The rest can remain as is or do minimal stubs.

        // We'll keep the chunk states or GPU buffers irrelevant for LOD usage.
        // This chunk is purely for meshing data.

        // Our real chunk dimension constants are not used by the mesher, but we
        // store them for reference here:
        int m_sizeX, m_sizeY, m_sizeZ;
        int m_offsetX, m_offsetY, m_offsetZ;
        const std::vector<int>& m_data;
    };

    // -------------------------------------------------------
    // 2) Instantiate our mini chunk
    // -------------------------------------------------------
    MiniChunk miniChunk(
        coarseSizeX, coarseSizeY, coarseSizeZ,
        offsetX, offsetY, offsetZ,
        coarseVoxels
    );

    // -------------------------------------------------------
    // 3) We need a minimal "manager" for boundary checks.
    //    Our GreedyMesher does "manager.getChunk(nx, ny, nz)" for neighbor lookups.
    //    We'll return null for all neighbors, so outside is air.
    // -------------------------------------------------------
    class MiniManager : public ChunkManager
    {
    public:
        // We'll hold a pointer to our mini chunk at (0,0,0).
        Chunk* m_chunkPtr = nullptr;

        // This is the only chunk that exists. If the caller
        // asks for chunk(0,0,0) => return our mini chunk, else null => air
        Chunk* getChunk(int cx, int cy, int cz) const override
        {
            if (cx == 0 && cy == 0 && cz == 0)
            {
                return m_chunkPtr;
            }
            return nullptr; // outside => air
        }

        // We won't implement createChunk/removeChunk etc.
        // We'll just rely on getChunk for neighbor checks.
    } miniManager;

    miniManager.m_chunkPtr = &miniChunk;

    // -------------------------------------------------------
    // 4) Create or retrieve a GreedyMesher
    //    If you have a global or shared instance, you can use that.
    //    Here we just create a local one.
    // -------------------------------------------------------
    GreedyMesher lodGreedy;

    // -------------------------------------------------------
    // 5) Call the mesher, telling it "this chunk is at (0,0,0)" in chunk coords,
    //    because we do the real world offset in the "offsetX, offsetY, offsetZ" param.
    // -------------------------------------------------------
    // The mesher's signature is:
    // generateMesh(Chunk& chunk, int cx, int cy, int cz,
    //     vector<Vertex>& outVertices, vector<uint32_t>& outIndices,
    //     int offsetX, int offsetY, int offsetZ,
    //     const ChunkManager& manager) const
    //
    // We'll pass 0,0,0 for cx,cy,cz. The offset is our real chunk offset.
    //
    // We'll accumulate into outVerts/outIndices.

    lodGreedy.generateMesh(
        miniChunk,
        0, 0, 0,                // chunk coords
        outVerts, outIndices,
        offsetX, offsetY, offsetZ,
        miniManager
    );

    // Now outVerts/outIndices hold a "greedy" LOD mesh that uses real voxel colors
    // for the downsampled data, with minimal geometry.

    // Done. The rest of your code can handle outVerts/outIndices as usual.
}
