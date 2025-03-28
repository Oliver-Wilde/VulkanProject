#include "LODMesher.h"
#include "Engine/Voxels/Chunk.h"
#include "Engine/Voxels/ChunkManager.h"
#include "Engine/Voxels/Meshing/GreedyMesher.h" 
#include <algorithm>  // For std::min, etc.
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
        verts.reserve(4096); // optional heuristic
        std::vector<uint32_t> inds;
        inds.reserve(6144);

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
    for (int lodLevel = 1; lodLevel < MultiLODResult::MAX_LODS; lodLevel++)
    {
        // Factor is 2^lodLevel
        int factor = (1 << lodLevel);

        // If factor is bigger than chunk dimension => everything collapses to 1 voxel, no need to continue
        if (factor >= std::min({ Chunk::SIZE_X, Chunk::SIZE_Y, Chunk::SIZE_Z }))
        {
            break; // LOD beyond chunk size => no smaller detail needed
        }

        std::vector<int> coarseVoxels;
        downsampleChunkData(chunk, factor, coarseVoxels);

        // The coarser dimensions
        int cSizeX = Chunk::SIZE_X / factor;
        int cSizeY = Chunk::SIZE_Y / factor;
        int cSizeZ = Chunk::SIZE_Z / factor;

        std::vector<Vertex> verts;
        verts.reserve(1024); // smaller heuristic for LOD
        std::vector<uint32_t> inds;
        inds.reserve(1536);

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
    // factor=2 => each 2x2x2 block in the original chunk is 1 voxel in the result
    const int cSizeX = Chunk::SIZE_X / factor;
    const int cSizeY = Chunk::SIZE_Y / factor;
    const int cSizeZ = Chunk::SIZE_Z / factor;

    outVoxels.resize(cSizeX * cSizeY * cSizeZ, 0);

    // For example, a factor=2 region in the original chunk is size(2󫎾)=8 blocks.
    // We'll count frequency of each voxel ID, then pick the majority.

    for (int zz = 0; zz < cSizeZ; zz++)
    {
        for (int yy = 0; yy < cSizeY; yy++)
        {
            for (int xx = 0; xx < cSizeX; xx++)
            {
                // NxNxN region
                int startX = xx * factor;
                int startY = yy * factor;
                int startZ = zz * factor;

                // We'll store frequency in an std::unordered_map<voxelID, int>
                // or a std::map<voxelID,int> if you prefer. 
                // If factor=2, we have at most 8 blocks, so this is quite small overhead.
                std::unordered_map<int, int> freq;
                freq.reserve(factor * factor * factor);

                for (int dz = 0; dz < factor; dz++)
                {
                    for (int dy = 0; dy < factor; dy++)
                    {
                        for (int dx = 0; dx < factor; dx++)
                        {
                            int realX = startX + dx;
                            int realY = startY + dy;
                            int realZ = startZ + dz;
                            int id = src.getBlock(realX, realY, realZ);

                            // Increment frequency
                            freq[id]++;
                        }
                    }
                }

                // Find the ID with the largest frequency
                int chosenID = 0;
                int maxCount = 0;

                for (auto& kv : freq)
                {
                    if (kv.second > maxCount)
                    {
                        chosenID = kv.first;
                        maxCount = kv.second;
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

    // 1) Make a "MiniChunk" that holds the downsampled data
    class MiniChunk : public Chunk
    {
    public:
        MiniChunk(int sx, int sy, int sz,
            int ox, int oy, int oz,
            const std::vector<int>& dataRef)
            : Chunk(0, 0, 0)  // chunk coords not relevant here
            , m_sizeX(sx), m_sizeY(sy), m_sizeZ(sz)
            , m_offsetX(ox), m_offsetY(oy), m_offsetZ(oz)
            , m_data(dataRef)
        {
            // No real block array allocated. We'll just override getBlock().
        }

        virtual int getBlock(int x, int y, int z) const override
        {
            if (x < 0 || x >= m_sizeX ||
                y < 0 || y >= m_sizeY ||
                z < 0 || z >= m_sizeZ)
            {
                return 0; // treat out-of-bounds as air
            }
            // Index into the coarser array
            int idx = (z * m_sizeY + y) * m_sizeX + x;
            return m_data[idx];
        }

        int m_sizeX, m_sizeY, m_sizeZ;
        int m_offsetX, m_offsetY, m_offsetZ;
        const std::vector<int>& m_data;
    };

    MiniChunk miniChunk(
        coarseSizeX, coarseSizeY, coarseSizeZ,
        offsetX, offsetY, offsetZ,
        coarseVoxels
    );

    // 2) Minimal manager => returns only miniChunk for (0,0,0)
    class MiniManager : public ChunkManager
    {
    public:
        Chunk* m_chunkPtr = nullptr;
        Chunk* getChunk(int cx, int cy, int cz) const override
        {
            if (cx == 0 && cy == 0 && cz == 0) {
                return m_chunkPtr;
            }
            return nullptr; // everything else => air
        }
    } miniManager;

    miniManager.m_chunkPtr = &miniChunk;

    // 3) Use a local GreedyMesher for the coarser data
    GreedyMesher lodGreedy;

    lodGreedy.generateMesh(
        miniChunk,
        0, 0, 0, // chunk coords
        outVerts, outIndices,
        offsetX, offsetY, offsetZ,
        miniManager
    );
}
