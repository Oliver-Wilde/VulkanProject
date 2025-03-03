#include "VoxelWorld.h"
#include "Chunk.h"
#include <cmath>
#include <stdexcept>
#include <chrono>
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Utils/Logger.h"
#include "Engine/Utils/ThreadPool.h"
#include "LODDownsampler.h"

extern ThreadPool g_threadPool;

// Timing stats for meshing
static double s_totalMeshTime = 0.0;
static int    s_meshCount = 0;

// A struct for passing meshing results back from worker threads
struct LODMeshBuildResult
{
    Chunk* chunkPtr = nullptr;
    int    cx = 0, cy = 0, cz = 0;
    int    lodLevel = 0;
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
};

// If you want to queue seam building results similarly
struct SeamBuildResult
{
    Chunk* chunkPtr = nullptr;
    Chunk::SeamDirection direction;
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
};

// Global queues for results
static std::mutex s_resultMutexLOD;
static std::vector<LODMeshBuildResult> s_pendingLODResults;

static std::mutex s_resultMutexSeam;
static std::vector<SeamBuildResult> s_pendingSeamResults;

// Marks neighbors dirty so we don’t see gaps at chunk boundaries
static void markNeighborsDirty(ChunkManager& manager, int cx, int cy, int cz)
{
    static const int offsets[6][3] = {
        {1,0,0}, {-1,0,0},
        {0,1,0}, {0,-1,0},
        {0,0,1}, {0,0,-1}
    };
    for (auto& off : offsets)
    {
        int nx = cx + off[0];
        int ny = cy + off[1];
        int nz = cz + off[2];
        if (manager.hasChunk(nx, ny, nz))
        {
            Chunk* nChunk = manager.getChunk(nx, ny, nz);
            if (nChunk) {
                nChunk->markAllLODsDirty();
                // Also mark all seams dirty if you want
                // nChunk->markAllSeamsDirty();
            }
        }
    }
}

// ------------------------------------------------
// getAvgMeshTime
// ------------------------------------------------
double VoxelWorld::getAvgMeshTime()
{
    if (s_meshCount == 0) return 0.0;
    return s_totalMeshTime / s_meshCount;
}

// ------------------------------------------------
// Constructor / Destructor
// ------------------------------------------------
VoxelWorld::VoxelWorld(VulkanContext* context)
    : m_context(context)
{
}

VoxelWorld::~VoxelWorld()
{
    // Destroy GPU buffers for all chunks
    auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks) {
        Chunk* c = kv.second.get();
        if (c) {
            for (int L = 0; L < LOD_COUNT; L++) {
                destroyChunkLOD(*c, L);
            }
            // Also destroy seam buffers
            for (int s = 0; s < 6; s++) {
                destroyChunkSeam(*c, static_cast<Chunk::SeamDirection>(s));
            }
        }
    }
}

// ------------------------------------------------
// initWorld
//  Spawns an initial region of chunks around (0,0)
//  using a single vertical layer at cy=0
// ------------------------------------------------
void VoxelWorld::initWorld()
{
    Logger::Info("initWorld() => Generating initial region at (0,0).");
    for (int cx = -VIEW_DISTANCE; cx <= VIEW_DISTANCE; ++cx)
    {
        for (int cz = -VIEW_DISTANCE; cz <= VIEW_DISTANCE; ++cz)
        {
            int cy = 0;
            Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

            // Queue background generation
            g_threadPool.enqueueTask([this, cx, cy, cz, newChunk]()
                {
                    m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);
                    newChunk->markAllLODsDirty();

                    std::lock_guard<std::mutex> lock(m_neighborMutex);
                    m_pendingNeighborDirty.emplace_back(cx, cy, cz);
                });
        }
    }
}

// ------------------------------------------------
// updateChunksAroundPlayer
//  Each frame, spawn new chunks near the player,
//  remove far ones, handle neighbors, etc.
// ------------------------------------------------
void VoxelWorld::updateChunksAroundPlayer(float playerPosX, float playerPosZ)
{
    int centerChunkX = (int)std::floor(playerPosX / (float)Chunk::SIZE_X);
    int centerChunkZ = (int)std::floor(playerPosZ / (float)Chunk::SIZE_Z);

    // 1) Create missing chunks
    for (int cx = centerChunkX - VIEW_DISTANCE; cx <= centerChunkX + VIEW_DISTANCE; cx++)
    {
        for (int cz = centerChunkZ - VIEW_DISTANCE; cz <= centerChunkZ + VIEW_DISTANCE; cz++)
        {
            int cy = 0;
            if (!m_chunkManager.hasChunk(cx, cy, cz))
            {
                Logger::Info("Needs chunk at ("
                    + std::to_string(cx) + ","
                    + std::to_string(cy) + ","
                    + std::to_string(cz) + ")");
                Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

                g_threadPool.enqueueTask([this, cx, cy, cz, newChunk]()
                    {
                        m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);
                        newChunk->markAllLODsDirty();

                        std::lock_guard<std::mutex> lock(m_neighborMutex);
                        m_pendingNeighborDirty.emplace_back(cx, cy, cz);
                    });
            }
        }
    }

    // 2) Unload out-of-range
    {
        std::vector<ChunkCoord> toRemove;
        const auto& allChunks = m_chunkManager.getAllChunks();
        for (auto& kv : allChunks) {
            const ChunkCoord& cc = kv.first;

            int distX = std::abs(cc.x - centerChunkX);
            int distZ = std::abs(cc.z - centerChunkZ);
            if (distX > VIEW_DISTANCE || distZ > VIEW_DISTANCE) {
                toRemove.push_back(cc);
            }
        }

        for (auto& rc : toRemove) {
            Chunk* oldC = m_chunkManager.getChunk(rc.x, rc.y, rc.z);
            if (oldC) {
                vkDeviceWaitIdle(m_context->getDevice());
                for (int L = 0; L < LOD_COUNT; L++) {
                    destroyChunkLOD(*oldC, L);
                }
                for (int s = 0; s < 6; s++) {
                    destroyChunkSeam(*oldC, static_cast<Chunk::SeamDirection>(s));
                }
                m_chunkManager.removeChunk(rc.x, rc.y, rc.z);
            }
        }
    }

    // 3) Mark neighbors dirty if needed
    {
        std::lock_guard<std::mutex> lock(m_neighborMutex);
        for (auto& cCoord : m_pendingNeighborDirty) {
            markNeighborsDirty(m_chunkManager, cCoord.x, cCoord.y, cCoord.z);
        }
        m_pendingNeighborDirty.clear();
    }

    // 4) Schedule meshing
    scheduleMeshingForDirtyChunks(centerChunkX, centerChunkZ);

    // 5) Poll results
    pollMeshBuildResults();
}

// ------------------------------------------------
// scheduleMeshingForDirtyChunks
// We adopt "max LOD difference = 1" across neighbors.
// We'll pick an LOD, but ensure we never have e.g. LOD0 next to LOD2.
// ------------------------------------------------
void VoxelWorld::scheduleMeshingForDirtyChunks(int centerChunkX, int centerChunkZ)
{
    const auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks)
    {
        const ChunkCoord& coord = kv.first;
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;

        // Skip if uploading 
        if (chunk->isUploading()) {
            continue;
        }

        // Check if *any* LOD is dirty
        bool anyDirty = false;
        int firstDirtyLOD = -1;
        for (int L = 0; L < LOD_COUNT; L++) {
            if (chunk->isLODDirty(L)) {
                anyDirty = true;
                if (firstDirtyLOD < 0) {
                    firstDirtyLOD = L;
                }
            }
        }
        if (!anyDirty) {
            continue;
        }

        // distance^2 
        int dx = coord.x - centerChunkX;
        int dz = coord.z - centerChunkZ;
        int distSq = dx * dx + dz * dz;

        // Decide LOD normally
        int chosenLOD = 0;
        if (distSq > 25) chosenLOD = 1;
        if (distSq > 100) chosenLOD = 2;

        // But ensure no neighbor is 2 levels away
        // Example: if we pick chosenLOD=2 but a neighbor is LOD0, that's a problem.
        // We'll check neighbors. 
        // (This is a naive approach. For full correctness, we might need an iterative pass.)
        static const int offsets[6][3] = {
            {1,0,0}, {-1,0,0},
            {0,1,0}, {0,-1,0},
            {0,0,1}, {0,0,-1}
        };

        for (auto& off : offsets)
        {
            int nx = coord.x + off[0];
            int ny = coord.y + off[1];
            int nz = coord.z + off[2];
            if (!m_chunkManager.hasChunk(nx, ny, nz))
                continue;

            Chunk* neighbor = m_chunkManager.getChunk(nx, ny, nz);
            if (!neighbor) continue;

            // Check each neighbor's highest valid LOD 
            // (or some approach to see what neighbor is building)
            // We'll do a quick approach:
            for (int L = 0; L < LOD_COUNT; L++)
            {
                // If neighbor has that LOD data valid => we see what L is
                // But if they're dirty, this is in flux. Real solutions do more robust logic.
                const auto& nLOD = neighbor->getLODData(L);
                if (nLOD.valid)
                {
                    // If difference > 1 => clamp chosenLOD
                    int diff = std::abs(L - chosenLOD);
                    if (diff > 1)
                    {
                        // E.g. if L=0, chosenLOD=2 => that's 2 levels difference.
                        // We can clamp to 1 for now.
                        chosenLOD = (chosenLOD > L) ? (L + 1) : (L - 1);
                    }
                }
            }
        }

        // Mark chunk as uploading 
        chunk->setIsUploading(true);

        // Clear other LOD dirty except chosen
        for (int L = 0; L < LOD_COUNT; L++) {
            if (L != chosenLOD) {
                chunk->clearLODDirty(L);
            }
        }

        int offsetX = coord.x * Chunk::SIZE_X;
        int offsetY = coord.y * Chunk::SIZE_Y;
        int offsetZ = coord.z * Chunk::SIZE_Z;

        // Submit a meshing job
        g_threadPool.enqueueTask([this, chunk, coord, chosenLOD, offsetX, offsetY, offsetZ]()
            {
                auto t0 = std::chrono::high_resolution_clock::now();

                std::vector<LODMeshBuildResult> localResults;

                if (chunk->isLODDirty(chosenLOD))
                {
                    chunk->clearLODDirty(chosenLOD);

                    // Build geometry
                    std::vector<Vertex> verts;
                    std::vector<uint32_t> inds;

                    if (chosenLOD == 0)
                    {
                        m_mesher.generateMeshGreedy(
                            *chunk,
                            coord.x, coord.y, coord.z,
                            verts, inds,
                            offsetX, offsetY, offsetZ,
                            m_chunkManager
                        );
                    }
                    else
                    {
                        const std::vector<int>& fullData = chunk->getBlocks();
                        std::vector<int> dsData = downsampleVoxelData(
                            fullData,
                            Chunk::SIZE_X,
                            Chunk::SIZE_Y,
                            Chunk::SIZE_Z,
                            chosenLOD
                        );

                        int dsX = Chunk::SIZE_X >> chosenLOD;
                        int dsY = Chunk::SIZE_Y >> chosenLOD;
                        int dsZ = Chunk::SIZE_Z >> chosenLOD;

                        m_mesher.generateMeshFromArray(
                            dsData, dsX, dsY, dsZ,
                            offsetX, offsetY, offsetZ,
                            verts, inds,
                            true /* useGreedy */
                        );
                    }

                    LODMeshBuildResult res;
                    res.chunkPtr = chunk;
                    res.cx = coord.x;
                    res.cy = coord.y;
                    res.cz = coord.z;
                    res.lodLevel = chosenLOD;
                    res.verts = std::move(verts);
                    res.inds = std::move(inds);
                    localResults.push_back(std::move(res));
                }

                auto t1 = std::chrono::high_resolution_clock::now();
                double elapsedSec = std::chrono::duration<double>(t1 - t0).count();
                s_totalMeshTime += elapsedSec;
                s_meshCount++;

                // Transfer results to global queue
                {
                    std::lock_guard<std::mutex> guard(s_resultMutexLOD);
                    for (auto& r : localResults) {
                        s_pendingLODResults.push_back(std::move(r));
                    }
                }
            });
    }
}

// ------------------------------------------------
// pollMeshBuildResults
//  Copy finished geometry from workers -> GPU
//  Then handle seam building if needed
// ------------------------------------------------
void VoxelWorld::pollMeshBuildResults()
{
    std::vector<LODMeshBuildResult> localCopy;
    {
        std::lock_guard<std::mutex> guard(s_resultMutexLOD);
        if (!s_pendingLODResults.empty()) {
            localCopy.swap(s_pendingLODResults);
        }
    }

    // 1) Upload LOD geometry
    for (auto& res : localCopy)
    {
        if (!res.chunkPtr) continue;
        Chunk* c = res.chunkPtr;

        if (!res.verts.empty() && !res.inds.empty())
        {
            Logger::Info("Finalizing LOD " + std::to_string(res.lodLevel)
                + " for chunk(" + std::to_string(res.cx) + ","
                + std::to_string(res.cy) + ","
                + std::to_string(res.cz) + ") => "
                + std::to_string(res.verts.size()) + " verts, "
                + std::to_string(res.inds.size()) + " inds");

            destroyChunkLOD(*c, res.lodLevel);
            uploadLODMeshToChunk(*c, res.lodLevel, res.verts, res.inds);
            c->getLODData(res.lodLevel).valid = true;
        }
        else
        {
            destroyChunkLOD(*c, res.lodLevel);
        }
        c->setIsUploading(false);
    }

    // 2) (Optional) Build seam geometry in a background job
    // For example, for each chunk that got updated, check neighbors with different LOD,
    // and if difference == 1, build the seam. 
    // We'll skip a full example here. You might queue tasks to produce SeamBuildResult.

    // 3) Poll seam results 
    // (In a real engine, you'd do something similar to the LOD approach.)
    {
        std::lock_guard<std::mutex> guard(s_resultMutexSeam);
        // If any seam results exist, we upload them:
        // (Implementation left out for brevity.)
    }
}

// ------------------------------------------------
// buildSeamBetweenChunks
//  Placeholder for the code that calculates bridging geometry
//  between chunkA (lodA) and chunkB (lodB) at a given face.
// ------------------------------------------------
void VoxelWorld::buildSeamBetweenChunks(Chunk& chunkA, int lodA,
    Chunk& chunkB, int lodB,
    int faceDirection)
{
    // ------------------------------------------------------------
    // 1) We’ll demonstrate faceDirection == +X specifically:
    //    chunkB is at (chunkA.worldX + 1, chunkA.worldY, chunkA.worldZ).
    // ------------------------------------------------------------
    if (faceDirection != /* +X */ 0) // or however you define +X
    {
        Logger::Info("buildSeamBetweenChunks() => Only handle +X in this example. Skipping.");
        return;
    }

    // If the LOD difference isn’t exactly 1, skip or do nothing
    // e.g. if |lodA - lodB| != 1, we might not build a seam.
    if (std::abs(lodA - lodB) != 1)
    {
        Logger::Info("buildSeamBetweenChunks() => LOD difference != 1, skipping seam.");
        return;
    }

    // Decide which chunk is finer (lower LOD index => higher resolution).
    // e.g. LOD0 < LOD1 < LOD2
    Chunk* finerChunk = (lodA < lodB ? &chunkA : &chunkB);
    Chunk* coarserChunk = (finerChunk == &chunkA ? &chunkB : &chunkA);

    int finerLOD = (lodA < lodB ? lodA : lodB);
    int coarserLOD = (finerLOD == lodA ? lodB : lodA);

    // We'll store the new geometry in the chunk that is finer,
    // on the face that abuts the other chunk.  This is arbitrary;
    // you could store in either chunk or both.
    // If chunkA is the finer side, that means chunkA's +X seam data.
    // If chunkB is the finer side, that means chunkB's -X seam data, etc.

    // In this example, we'll assume chunkA is "the chunk with faceDirection = +X",
    // so if chunkA is the finer chunk, we store seam in chunkA's SEAM_POS_X.
    // Otherwise, if chunkB is the finer chunk, store in chunkB's SEAM_NEG_X.

    Chunk::SeamDirection seamDirForFiner;
    if (finerChunk == &chunkA)
    {
        // chunkA is on the left, chunkB is on the right => chunkA’s seam is +X
        seamDirForFiner = Chunk::SEAM_POS_X;
    }
    else
    {
        // chunkB is the finer chunk => chunkB's seam is -X
        seamDirForFiner = Chunk::SEAM_NEG_X;
    }

    // ------------------------------------------------------------
    // 2) Figure out how many samples (subdivisions) each chunk’s boundary has
    //    If chunk resolution is 16x16 for LOD0, that means along +X face,
    //    we have 16 segments in Z direction (since X=16 is the boundary).
    //
    //    For LOD1, we'd have half that resolution, so 8 segments, etc.
    //    In general, #samples = 1 << (4 - lodLevel) if SIZE_X=16. 
    //    (But let's do it formulaically.)
    // ------------------------------------------------------------
    int finerResolution = (Chunk::SIZE_Z >> finerLOD);   // e.g. 16 >> 0 = 16
    int coarserResolution = (Chunk::SIZE_Z >> coarserLOD); // e.g. 16 >> 1 = 8

    // We’ll gather “surface height” along that boundary from each chunk.
    // The boundary we’re bridging is the line X=SIZE_X (for chunk with +X face)
    // or X=0 for chunk with -X face. For chunkB, if it’s coarser, we read from x=0.
    // If chunkB is the finer chunk, we read from x=16, etc. We'll define a small helper:

    auto getSurfaceHeight = [&](Chunk& c, int lodLevel, int localX, int localZ)->int
        {
            // This function finds the topmost block in that column, naive approach:
            // localX, localZ range depends on the chunk’s effective size at that LOD.
            // Typically the chunk has full data, so we can do a quick "downsample coordinate"
            // or read c.getBlock(...) if we want the actual block data.

            // For simplicity, we’ll do a direct scale:
            int factor = lodLevel; // e.g. LOD=1 => factor=1 => half
            // Or better: factor = (1 << lodLevel)

            // Convert localX in [0..(SIZE_Z>>lodLevel)] to the full chunk coordinate
            int fullZ = localZ << lodLevel;
            // We'll fix the X boundary: if it's the +X face, x=15 for LOD0, x=7 for LOD1, etc.
            // Actually let's do if we read the chunk’s entire block array at the real resolution, 
            // the boundary is x=Chunk::SIZE_X - 1 for +X.
            int boundaryX = (faceDirection == 0 /* +X */)
                ? (Chunk::SIZE_X - 1)
                : 0;
            if (c.worldX() > coarserChunk->worldX()) {
                // c is on the right => x=0 if we are reading chunk c for the -X face
            }

            // If we are generating for chunk c's +X boundary at LOD=0 => boundaryX=15
            // at LOD=1 => boundaryX=7 (?). This can get complicated quickly, so
            // let's keep it super naive: just always read x=SIZE_X-1 for +X face 
            // or x=0 for -X face in the chunk’s real block array.

            int highestY = -1;
            for (int y = Chunk::SIZE_Y - 1; y >= 0; y--)
            {
                int blockID = c.getBlock(boundaryX, y, fullZ);
                if (blockID > 0) // found a non-air block
                {
                    highestY = y;
                    break;
                }
            }
            return highestY < 0 ? 0 : highestY;
        };

    // Gather the boundary heights from the finer chunk in an array
    // e.g. size = finerResolution + 1 for the corners
    std::vector<int> finerHeights(finerResolution + 1, 0);

    // We'll do the same for coarser chunk => coarserResolution + 1
    std::vector<int> coarserHeights(coarserResolution + 1, 0);

    for (int i = 0; i <= finerResolution; i++)
    {
        finerHeights[i] = getSurfaceHeight(*finerChunk, finerLOD, /*localX*/ 0, /*localZ*/ i);
    }
    for (int i = 0; i <= coarserResolution; i++)
    {
        coarserHeights[i] = getSurfaceHeight(*coarserChunk, coarserLOD, /*localX*/ 0, /*localZ*/ i);
    }

    // ------------------------------------------------------------
    // 3) Build bridging geometry
    //    For each segment in the coarser boundary, we match it to
    //    two segments in the finer boundary. (Because it’s half resolution.)
    // ------------------------------------------------------------
    std::vector<Vertex> seamVerts;
    std::vector<uint32_t> seamIndices;

    // We'll define the base world offset for the "finer" chunk’s boundary in X.
    int chunkSizeX = Chunk::SIZE_X;
    int chunkSizeZ = Chunk::SIZE_Z;

    // We'll assume chunkX, chunkZ to place these points in world space.
    // E.g. if chunkA is at (cx, cz) => offsetX = cx*16, etc.
    // Actually we can do a quick approach using the chunk’s world coords:
    float finerWorldX = float(finerChunk->worldX() * Chunk::SIZE_X);
    float finerWorldZ = float(finerChunk->worldZ() * Chunk::SIZE_Z);

    float coarserWorldX = float(coarserChunk->worldX() * Chunk::SIZE_X);
    float coarserWorldZ = float(coarserChunk->worldZ() * Chunk::SIZE_Z);

    // If faceDirection = +X => the boundary is at x = chunkSizeX for chunk on left,
    // or x=0 for chunk on the right. We'll define that the bridging is in the Z direction.

    // We'll color them differently just to see
    float rf = 0.8f, gf = 0.2f, bf = 0.8f; // color for "finer"
    float rc = 0.2f, gc = 0.8f, bc = 0.2f; // color for "coarser"

    // coarserResolution * 2 => how many segments the finer boundary has in that region
    // For each coarser i in [0..coarserResolution-1], 
    // the corresponding finer i2 in [0..(2*coarserResolution)-1].
    // We'll produce a small quads bridging the two lines of heights.

    for (int i = 0; i < coarserResolution; i++)
    {
        // coarser segment is [i.. i+1]
        int h0c = coarserHeights[i];
        int h1c = coarserHeights[i + 1];

        // The corresponding finer range is [2*i.. 2*i+2]
        int iF0 = 2 * i;
        int iF1 = 2 * i + 2;

        if (iF1 > finerResolution) {
            // just clamp 
            iF1 = finerResolution;
        }

        int h0f = (iF0 <= finerResolution) ? finerHeights[iF0] : finerHeights.back();
        int h1f = (iF1 <= finerResolution) ? finerHeights[iF1] : finerHeights.back();

        // Now we have 2 vertical “columns” => coarser at z0..z1, finer at z0..z2
        // We'll define the actual z positions in world space:
        float z0c = coarserWorldZ + float(i) * (float(Chunk::SIZE_Z) / coarserResolution);
        float z1c = coarserWorldZ + float(i + 1) * (float(Chunk::SIZE_Z) / coarserResolution);
        float z0f = finerWorldZ + float(iF0) * (float(Chunk::SIZE_Z) / finerResolution);
        float z1f = finerWorldZ + float(iF1) * (float(Chunk::SIZE_Z) / finerResolution);

        // The x positions for the seam if the finer chunk is to the left => x=some boundary
        // We'll do xF = (finer chunk's worldX + chunkSizeX - 0.1f) 
        // and xC = (coarser chunk's worldX + 0.1f) for a small bridging. 
        // Or just do the same x if you want no gap.

        float xF = float(finerChunk->worldX() * Chunk::SIZE_X + Chunk::SIZE_X);
        float xC = float(coarserChunk->worldX() * Chunk::SIZE_X);

        // We'll build two quads bridging these columns:
        // e.g. the geometry in the XZ plane, y=height. 
        // It's basically a trapezoid between (xF, h0f, z0f) and (xC, h0c, z0c).

        // For simplicity, let's build a single big quad bridging
        // coarser (i.. i+1) to finer (2i.. 2i+2).
        // We'll do it in two triangles.

        // We'll define 4 corners:
        //  corner0 => coarser i => (xC, h0c, z0c)
        //  corner1 => coarser i+1 => (xC, h1c, z1c)
        //  corner2 => finer iF0 => (xF, h0f, z0f)
        //  corner3 => finer iF1 => (xF, h1f, z1f)

        // We'll do something like:
        int baseIndex = (int)seamVerts.size();

        // corner0 coarser
        seamVerts.push_back(Vertex(
            xC, (float)h0c, z0c,
            rc, gc, bc
        ));
        // corner1 coarser
        seamVerts.push_back(Vertex(
            xC, (float)h1c, z1c,
            rc, gc, bc
        ));
        // corner2 finer
        seamVerts.push_back(Vertex(
            xF, (float)h0f, z0f,
            rf, gf, bf
        ));
        // corner3 finer
        seamVerts.push_back(Vertex(
            xF, (float)h1f, z1f,
            rf, gf, bf
        ));

        // Now build 2 triangles: (0,1,2) and (2,1,3)
        seamIndices.push_back(baseIndex + 0);
        seamIndices.push_back(baseIndex + 1);
        seamIndices.push_back(baseIndex + 2);

        seamIndices.push_back(baseIndex + 2);
        seamIndices.push_back(baseIndex + 1);
        seamIndices.push_back(baseIndex + 3);
    }

    // ------------------------------------------------------------
    // 4) Upload to the “finer” chunk’s seam buffer
    //    If chunkA is the finer, we do chunkA->SEAM_POS_X,
    //    else chunkB->SEAM_NEG_X, etc.
    // ------------------------------------------------------------
    if (!seamVerts.empty() && !seamIndices.empty())
    {
        Logger::Info("buildSeamBetweenChunks => Building seam with "
            + std::to_string(seamVerts.size()) + " verts, "
            + std::to_string(seamIndices.size()) + " inds.");

        // But recall we’re inside VoxelWorld, so we can call:
        destroyChunkSeam(*finerChunk, seamDirForFiner); // remove old if any
        uploadSeamMeshToChunk(*finerChunk, seamDirForFiner,
            seamVerts, seamIndices);
    }
    else
    {
        // no geometry => destroy if any
        destroyChunkSeam(*finerChunk, seamDirForFiner);
    }
}

// ------------------------------------------------
// uploadLODMeshToChunk
// ------------------------------------------------
void VoxelWorld::uploadLODMeshToChunk(
    Chunk& chunk,
    int lodLevel,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds)
{
    VkDeviceSize vbSize = sizeof(Vertex) * verts.size();
    VkDeviceSize ibSize = sizeof(uint32_t) * inds.size();

    // 1) Create device-local buffers
    VkBuffer       newVB = VK_NULL_HANDLE;
    VkDeviceMemory newVBMem = VK_NULL_HANDLE;
    VkBuffer       newIB = VK_NULL_HANDLE;
    VkDeviceMemory newIBMem = VK_NULL_HANDLE;

    createBuffer(vbSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        newVB, newVBMem);

    createBuffer(ibSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        newIB, newIBMem);

    // 2) Staging
    VkBuffer stagingVB = VK_NULL_HANDLE;
    VkDeviceMemory stagingVBMem = VK_NULL_HANDLE;
    createBuffer(vbSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        stagingVB, stagingVBMem);

    VkBuffer stagingIB = VK_NULL_HANDLE;
    VkDeviceMemory stagingIBMem = VK_NULL_HANDLE;
    createBuffer(ibSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        stagingIB, stagingIBMem);

    // 3) Copy CPU => staging
    {
        void* dataPtr = nullptr;
        vkMapMemory(m_context->getDevice(), stagingVBMem, 0, vbSize, 0, &dataPtr);
        memcpy(dataPtr, verts.data(), (size_t)vbSize);
        vkUnmapMemory(m_context->getDevice(), stagingVBMem);

        vkMapMemory(m_context->getDevice(), stagingIBMem, 0, ibSize, 0, &dataPtr);
        memcpy(dataPtr, inds.data(), (size_t)ibSize);
        vkUnmapMemory(m_context->getDevice(), stagingIBMem);
    }

    // 4) Transfer
    copyBuffer(stagingVB, newVB, vbSize);
    copyBuffer(stagingIB, newIB, ibSize);

    // 5) Destroy staging
    {
        VkDevice dev = m_context->getDevice();
        vkDestroyBuffer(dev, stagingVB, nullptr);
        vkFreeMemory(dev, stagingVBMem, nullptr);
        vkDestroyBuffer(dev, stagingIB, nullptr);
        vkFreeMemory(dev, stagingIBMem, nullptr);
    }

    // 6) Assign
    auto& lodData = chunk.getLODData(lodLevel);
    lodData.vertexBuffer = newVB;
    lodData.vertexMemory = newVBMem;
    lodData.indexBuffer = newIB;
    lodData.indexMemory = newIBMem;
    lodData.vertexCount = (uint32_t)verts.size();
    lodData.indexCount = (uint32_t)inds.size();
    lodData.valid = true;
}

// ------------------------------------------------
// uploadSeamMeshToChunk
//  A separate function to store the bridging geometry
//  in chunk’s seam data at a certain face.
// ------------------------------------------------
void VoxelWorld::uploadSeamMeshToChunk(Chunk& chunk,
    Chunk::SeamDirection seamDir,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds)
{
    // Very similar to uploadLODMeshToChunk, but writes to chunk.getSeamData(dir).
    // We'll do a minimal version here:

    VkDeviceSize vbSize = sizeof(Vertex) * verts.size();
    VkDeviceSize ibSize = sizeof(uint32_t) * inds.size();

    VkBuffer       newVB = VK_NULL_HANDLE;
    VkDeviceMemory newVBMem = VK_NULL_HANDLE;
    VkBuffer       newIB = VK_NULL_HANDLE;
    VkDeviceMemory newIBMem = VK_NULL_HANDLE;

    createBuffer(vbSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        newVB, newVBMem);
    createBuffer(ibSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        newIB, newIBMem);

    // Staging
    VkBuffer stagingVB = VK_NULL_HANDLE;
    VkDeviceMemory stagingVBMem = VK_NULL_HANDLE;
    createBuffer(vbSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        stagingVB, stagingVBMem);

    VkBuffer stagingIB = VK_NULL_HANDLE;
    VkDeviceMemory stagingIBMem = VK_NULL_HANDLE;
    createBuffer(ibSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        stagingIB, stagingIBMem);

    {
        void* dataPtr = nullptr;
        vkMapMemory(m_context->getDevice(), stagingVBMem, 0, vbSize, 0, &dataPtr);
        memcpy(dataPtr, verts.data(), (size_t)vbSize);
        vkUnmapMemory(m_context->getDevice(), stagingVBMem);

        vkMapMemory(m_context->getDevice(), stagingIBMem, 0, ibSize, 0, &dataPtr);
        memcpy(dataPtr, inds.data(), (size_t)ibSize);
        vkUnmapMemory(m_context->getDevice(), stagingIBMem);
    }

    copyBuffer(stagingVB, newVB, vbSize);
    copyBuffer(stagingIB, newIB, ibSize);

    {
        VkDevice dev = m_context->getDevice();
        vkDestroyBuffer(dev, stagingVB, nullptr);
        vkFreeMemory(dev, stagingVBMem, nullptr);
        vkDestroyBuffer(dev, stagingIB, nullptr);
        vkFreeMemory(dev, stagingIBMem, nullptr);
    }

    // Store
    auto& seamData = chunk.getSeamData(seamDir);
    seamData.seamVertexBuffer = newVB;
    seamData.seamVertexMemory = newVBMem;
    seamData.seamIndexBuffer = newIB;
    seamData.seamIndexMemory = newIBMem;
    seamData.vertexCount = (uint32_t)verts.size();
    seamData.indexCount = (uint32_t)inds.size();
    seamData.valid = true;
}

// ------------------------------------------------
// destroyChunkLOD
// ------------------------------------------------
void VoxelWorld::destroyChunkLOD(Chunk& chunk, int lodLevel)
{
    auto& lodData = chunk.getLODData(lodLevel);
    VkDevice device = m_context->getDevice();

    if (lodData.vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, lodData.vertexBuffer, nullptr);
        lodData.vertexBuffer = VK_NULL_HANDLE;
    }
    if (lodData.vertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, lodData.vertexMemory, nullptr);
        lodData.vertexMemory = VK_NULL_HANDLE;
    }
    if (lodData.indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, lodData.indexBuffer, nullptr);
        lodData.indexBuffer = VK_NULL_HANDLE;
    }
    if (lodData.indexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, lodData.indexMemory, nullptr);
        lodData.indexMemory = VK_NULL_HANDLE;
    }
    lodData.vertexCount = 0;
    lodData.indexCount = 0;
    lodData.valid = false;
}

// ------------------------------------------------
// destroyChunkSeam
// ------------------------------------------------
void VoxelWorld::destroyChunkSeam(Chunk& chunk, Chunk::SeamDirection dir)
{
    auto& seamData = chunk.getSeamData(dir);
    VkDevice device = m_context->getDevice();

    if (seamData.seamVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, seamData.seamVertexBuffer, nullptr);
        seamData.seamVertexBuffer = VK_NULL_HANDLE;
    }
    if (seamData.seamVertexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, seamData.seamVertexMemory, nullptr);
        seamData.seamVertexMemory = VK_NULL_HANDLE;
    }
    if (seamData.seamIndexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, seamData.seamIndexBuffer, nullptr);
        seamData.seamIndexBuffer = VK_NULL_HANDLE;
    }
    if (seamData.seamIndexMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, seamData.seamIndexMemory, nullptr);
        seamData.seamIndexMemory = VK_NULL_HANDLE;
    }
    seamData.vertexCount = 0;
    seamData.indexCount = 0;
    seamData.valid = false;
}

// ------------------------------------------------
// createBuffer, copyBuffer, findMemoryType
// ------------------------------------------------
void VoxelWorld::createBuffer(VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& memory)
{
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &bufInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_context->getDevice(), buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

    if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(m_context->getDevice(), buffer, memory, 0);
}

void VoxelWorld::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    VkCommandPool cmdPool = m_context->getCommandPool();
    VkQueue       gfxQueue = m_context->getGraphicsQueue();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = cmdPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo, &cmdBuf);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmdBuf, src, dst, 1, &copyRegion);

    vkEndCommandBuffer(cmdBuf);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    vkQueueSubmit(gfxQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(gfxQueue);

    vkFreeCommandBuffers(m_context->getDevice(), cmdPool, 1, &cmdBuf);
}

uint32_t VoxelWorld::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((filter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}
