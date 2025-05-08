#include "VoxelWorld.h"
#include "Chunk.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Resources/ResourceManager.h"
#include "Engine/Utils/Logger.h"
#include "Engine/Utils/ThreadPool.h"
#include "Engine/Voxels/Meshing/GreedyMesher.h"
#include "Engine/Voxels/Meshing/NaiveMesher.h"
#include "Engine/Voxels/Meshing/LODMesher.h"
#include "Engine/Graphics/Renderer.h"  // ring-buffer logic
#include "Engine/Utils/CpuProfiler.h"  // for CpuProfiler::ScopedTimer
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <chrono>
#include <mutex>
#include <thread>
#include <sstream>
#include <numeric>

#include "Engine/Globals/BenchFlags.h"

#ifdef BENCHMARK_MODE
#include "Engine/Utils/BenchmarkLogger.h"   // ← gives us ChunkLogRow
#endif



using Clock = std::chrono::high_resolution_clock;
// Externally declared:
extern ThreadPool g_threadPool;

// ------------------------------------------------------------------------
// Stats for measuring meshing times
// ------------------------------------------------------------------------
static double s_totalMeshTime = 0.0;
static int    s_meshCount = 0;

// Protect multi-lod results
static std::mutex s_resultMutex;
static std::vector<MultiLODResult> s_pendingLODResults;
#ifdef BENCHMARK_MODE
static inline uint64_t split21(uint32_t v)
{
    uint64_t x = v & 0x1fffff;
    x = (x | (x << 32)) & 0x1f00000000ffffULL;
    x = (x | (x << 16)) & 0x1f0000ff0000ffULL;
    x = (x | (x << 8)) & 0x100f00f00f00f00fULL;
    x = (x | (x << 4)) & 0x10c30c30c30c30c3ULL;
    x = (x | (x << 2)) & 0x1249249249249249ULL;
    return x;
}
static inline uint64_t encodeMorton21(int x, int y, int z)
{
    return split21(uint32_t(x)) | (split21(uint32_t(y)) << 1) | (split21(uint32_t(z)) << 2);
}
/* frame counter local to VoxelWorld → increments once per frame           */
static uint32_t s_worldFrameNumber = 0;
#endif

static float averageDequeMs(const std::deque<float>& d)
{
    if (d.empty()) return 0.f;
    return std::accumulate(d.begin(), d.end(), 0.f) / float(d.size());
}
// ------------------------------------------------------------------------
// Constructor / Destructor
// ------------------------------------------------------------------------
VoxelWorld::VoxelWorld(VulkanContext* ctx, ResourceManager* rm)
    : m_context(ctx)
    , m_resourceManager(rm)
{
    CpuProfiler::ScopedTimer _t("VoxelWorld::VoxelWorld");
    m_lastFrameTS = Clock::now();

    /* baseline upload-budget comes from the CLI flag (--upload-mib) */
    m_uploadBudgetBytes = static_cast<size_t>(g_cliUploadMB) * 1024 * 1024;
    m_uploadBudgetChunks = 5;   // keep previous default chunk-limit
}

VoxelWorld::~VoxelWorld()
{
    CpuProfiler::ScopedTimer _t("VoxelWorld::~VoxelWorld");
    const auto& all = m_chunkManager.getAllChunks();
    for (auto& kv : all)
    {
        if (!kv.second) continue;
        Chunk* c = kv.second.get();
        for (int lod = 0; lod < Chunk::MAX_LOD_LEVELS; ++lod)
        {
            auto& ld = c->getLODData(lod);
            if (!ld.vertexBuffer && !ld.indexBuffer) continue;

            if (m_renderer)
            {
                QueuedChunkDestruction q{ ld.vertexBuffer, ld.vertexMemory,
                                          ld.indexBuffer,  ld.indexMemory };
                m_renderer->enqueueDeferredDestroy(q);
            }
        }
    }
}


// ------------------------------------------------------------------------
// setRenderer
// ------------------------------------------------------------------------
void VoxelWorld::setRenderer(Renderer* r) { m_renderer = r; }

// ------------------------------------------------------------------------
// initWorld => generate initial region around (0,0,0) with multiple Y-layers
// ------------------------------------------------------------------------
void VoxelWorld::initWorld()
{
    CpuProfiler::ScopedTimer initTimer("VoxelWorld::initWorld");
    Logger::Info("VoxelWorld::initWorld – generating initial region.");

    /* pick mesher according to CLI flag ---------------------------------- */
    if (g_cliMesher == "naive")
        m_currentMesherType = MesherType::NAIVE;
    else
        m_currentMesherType = MesherType::GREEDY;

    int radius = g_cliViewDist;
    int cyMin = 0, cyMax = 1;

    for (int cx = -radius; cx <= radius; ++cx)
        for (int cz = -radius; cz <= radius; ++cz)
            for (int cy = cyMin; cy <= cyMax; ++cy)
            {
                auto chk = m_chunkManager.createChunk(cx, cy, cz);
                g_threadPool.enqueueTask([this, chk, cx, cy, cz]()
                    {
                        m_terrainGenerator.generateChunk(*chk, cx, cy, cz);
                        chk->markDirty();
                    }, TaskType::Generation, 100);
            }
}





// ------------------------------------------------------------------------
// updateChunksAroundPlayer => ring-buffer load/unload in all 3 dims
// ------------------------------------------------------------------------
void VoxelWorld::updateChunksAroundPlayer(float playerPosX, float playerPosZ)
{
#ifdef BENCHMARK_MODE
    m_cpuMeshingMsLastFrame = 0.0f;
    m_chunksRebuiltLastFrame = 0;
    ++s_worldFrameNumber;
#endif

    /* ── rolling frame-time ------------------------------------------------ */
    auto  now = Clock::now();
    float dtMs = std::chrono::duration<float, std::milli>(now - m_lastFrameTS).count();
    m_lastFrameTS = now;

    m_frameTimeMs.push_back(dtMs);
    if (m_frameTimeMs.size() > FRAME_TIME_BUFFER)
        m_frameTimeMs.pop_front();

    float avgMs = averageDequeMs(m_frameTimeMs);

    /* ── adaptive upload-budget (unchanged) -------------------------------- */

    /* ── periodic streaming update ---------------------------------------- */
    static const int UPDATE_INTERVAL = 10;   // update every 10 frames
    static int frameCounter = 0;
    if (frameCounter++ % UPDATE_INTERVAL == 0)
    {
        CpuProfiler::ScopedTimer t("VoxelWorld::chunkStreaming");

        /* centre chunk coordinates --------------------------------------- */
        int centerX = int(std::floor(playerPosX / float(Chunk::SIZE_X)));
        int centerZ = int(std::floor(playerPosZ / float(Chunk::SIZE_Z)));
        int centerY = 0;                       // single Y-layer for now

        const int radius = g_cliViewDist;   // <<< runtime flag
        const int verticalRange = 3;
        int minCy = centerY - verticalRange;
        int maxCy = centerY + verticalRange;

        /* 1) chunks to load ---------------------------------------------- */
        std::vector<ChunkCoord> toLoad;
        toLoad.reserve((2 * radius + 1) * (2 * verticalRange + 1) * (2 * radius + 1));

        for (int cx = centerX - radius; cx <= centerX + radius; ++cx)
            for (int cz = centerZ - radius; cz <= centerZ + radius; ++cz)
                for (int cy = minCy; cy <= maxCy; ++cy)
                    if (!m_chunkManager.hasChunk(cx, cy, cz))
                        toLoad.push_back({ cx, cy, cz });

        auto distSq = [&](const ChunkCoord& cc)
            {
                int dx = cc.x - centerX, dy = cc.y - centerY, dz = cc.z - centerZ;
                return dx * dx + dy * dy + dz * dz;
            };
        std::sort(toLoad.begin(), toLoad.end(),
            [&](auto& a, auto& b) { return distSq(a) < distSq(b); });

        for (auto& cc : toLoad) m_chunksToLoad.push_back(cc);

        /* 2) chunks to unload -------------------------------------------- */
        const auto& all = m_chunkManager.getAllChunks();
        for (auto& kv : all)
        {
            const ChunkCoord& cc = kv.first;
            int dx = std::abs(cc.x - centerX);
            int dy = std::abs(cc.y - centerY);
            int dz = std::abs(cc.z - centerZ);
            if (dx > radius || dz > radius || dy > verticalRange)
                m_chunksToUnload.push_back(cc);
        }
    }

    /* ── 3-5) load / unload / meshing / uploads  (unchanged) -------------- */
    const int LOAD_BUDGET = 64;
    const int UNLOAD_BUDGET = 64;

    int loaded = 0;
    while (!m_chunksToLoad.empty() && loaded < LOAD_BUDGET)
    {
        ChunkCoord c = m_chunksToLoad.front();
        m_chunksToLoad.pop_front();
        loadOneChunk(c);
        ++loaded;
    }

    int unloaded = 0;
    while (!m_chunksToUnload.empty() && unloaded < UNLOAD_BUDGET)
    {
        ChunkCoord c = m_chunksToUnload.front();
        m_chunksToUnload.pop_front();
        unloadOneChunk(c);
        ++unloaded;
    }

    scheduleMeshingForDirtyChunks();
    gatherMesherResults();
    drainUploadQueue();
}
// ------------------------------------------------------------------------
// loadOneChunk => create + queue generation
// ------------------------------------------------------------------------
void VoxelWorld::loadOneChunk(const ChunkCoord& c)
{
    CpuProfiler::ScopedTimer _t("VoxelWorld::loadOneChunk");

    if (m_chunkManager.hasChunk(c.x, c.y, c.z)) return;

    /* shared‑ptr keeps chunk alive until generation finishes */
    auto chunk = m_chunkManager.createChunk(c.x, c.y, c.z);

    g_threadPool.enqueueTask(
        [this, chunk, c]
        {
            m_terrainGenerator.generateChunk(*chunk, c.x, c.y, c.z);
            chunk->markDirty();
        },
        TaskType::Generation, 10);
}
// ------------------------------------------------------------------------
// unloadOneChunk => ring-buffer destroy & remove
// ------------------------------------------------------------------------
void VoxelWorld::unloadOneChunk(const ChunkCoord& c)
{
    CpuProfiler::ScopedTimer _t("VoxelWorld::unloadOneChunk");

    std::shared_ptr<Chunk> chunk = m_chunkManager.getChunk(c.x, c.y, c.z);
    if (!chunk) return;

    if (chunk->isUploading())
    {
        Logger::Info("unloadOneChunk – chunk uploading, re‑queue: "
            + std::to_string(c.x) + ","
            + std::to_string(c.y) + ","
            + std::to_string(c.z));
        m_chunksToUnload.push_back(c);
        return;
    }

    // Destroy all LOD GPU buffers ring‑buffer style
    if (m_renderer)
    {
        for (int lod = 0; lod < Chunk::MAX_LOD_LEVELS; lod++)
        {
            auto& ld = chunk->getLODData(lod);
            if (ld.vertexBuffer || ld.indexBuffer)
            {
                QueuedChunkDestruction qcd;
                qcd.vb = ld.vertexBuffer;  qcd.vbMem = ld.vertexMemory;
                qcd.ib = ld.indexBuffer;   qcd.ibMem = ld.indexMemory;
                m_renderer->enqueueDeferredDestroy(qcd);

                ld.vertexBuffer = VK_NULL_HANDLE;
                ld.vertexMemory = VK_NULL_HANDLE;
                ld.indexBuffer = VK_NULL_HANDLE;
                ld.indexMemory = VK_NULL_HANDLE;
                ld.vertexCount = 0;
                ld.indexCount = 0;
            }
        }
    }

    Logger::Info("unloadOneChunk => removing chunk (" +
        std::to_string(c.x) + "," +
        std::to_string(c.y) + "," +
        std::to_string(c.z) + ")");

    m_chunkManager.removeChunk(c.x, c.y, c.z);
}
// ------------------------------------------------------------------------scheduleMeshingForDirtyChunks
// scheduleMeshingForDirtyChunks => multi-lod only
// ------------------------------------------------------------------------
void VoxelWorld::scheduleMeshingForDirtyChunks()
{
    CpuProfiler::ScopedTimer _t("VoxelWorld::scheduleMeshing");

    const auto& all = m_chunkManager.getAllChunks();
    const IMesher* base =
        (m_currentMesherType == MesherType::GREEDY)
        ? static_cast<const IMesher*>(&m_greedyMesher)
        : static_cast<const IMesher*>(&m_naiveMesher);

    for (auto& kv : all)
    {
        std::shared_ptr<Chunk> c = kv.second;
        if (!c || !c->isDirty()) continue;

        // Skip uniform chunks
        if (c->getState() != Chunk::ChunkState::NORMAL)
        {
            c->clearDirty();
            continue;
        }

        // Per‑frame bounds refresh for culling
        c->recalcFilledBounds();
        c->clearDirty();
        c->setIsUploading(true);

        if (m_useMultiLOD)
        {
            /* existing multi‑LOD job */
            g_threadPool.enqueueTask(
                [this, c, base]()
                {
                    CpuProfiler::ScopedTimer tm("buildAllLODs");
                    MultiLODResult mlr = LODMesher::buildAllLODs(*c,
                        c->worldX(), c->worldY(), c->worldZ(),
                        base, m_chunkManager);

                    {
                        std::lock_guard<std::mutex> lk(s_resultMutex);
                        s_pendingLODResults.emplace_back(std::move(mlr));
                    }
                },
                TaskType::Meshing, 50);
        }
        else
        {
            /* single‑LOD job */
            scheduleMeshingSingleLOD(*c, base);
        }
    }
}

// ------------------------------------------------------------------------
// pollMeshBuildResults => finalize LOD GPU uploads
// ------------------------------------------------------------------------
void VoxelWorld::gatherMesherResults()
{
    if (m_useMultiLOD)
    {
        /* multi‑LOD path */
        std::vector<MultiLODResult> local;
        {
            std::lock_guard<std::mutex> lk(s_resultMutex);
            if (!s_pendingLODResults.empty())
                local.swap(s_pendingLODResults);
        }
        for (auto& r : local)
        {
            size_t bytes = 0;
            for (int i = 0; i < MultiLODResult::MAX_LODS; ++i)
            {
                bytes += r.lods[i].verts.size() * sizeof(Vertex);
                bytes += r.lods[i].inds.size() * sizeof(uint32_t);
            }
            m_uploadQueue.push_back({ std::move(r), bytes, 0 });
        }
    }
    else
    {
        gatherSingleLODResults();
    }
}

// ------------------------------------------------------------------------
// finalizeMultiLOD => put LOD geometry on the GPU
// ------------------------------------------------------------------------
void VoxelWorld::finalizeMultiLOD(MultiLODResult& mlr)
{
    Chunk* c = mlr.chunkPtr;
    if (!c) return;

    /* Count how many real uploads we’ll issue so we know when we’re done.   */
    int uploadsNeeded = 0;
    for (int i = 0; i < MultiLODResult::MAX_LODS; ++i)
        if (!mlr.lods[i].verts.empty() && !mlr.lods[i].inds.empty())
            ++uploadsNeeded;

    /* If nothing to upload we can immediately mark the chunk as ready.      */
    if (uploadsNeeded == 0)
    {
        for (int i = 0; i < MultiLODResult::MAX_LODS; ++i)
        {
            auto& old = c->getLODData(i);
            old.vertexBuffer = old.indexBuffer = VK_NULL_HANDLE;
            old.vertexMemory = old.indexMemory = VK_NULL_HANDLE;
            old.vertexCount = old.indexCount = 0;
            c->setLODGenerated(i, false);
            c->setLODErrorValue(i, 0.f);
        }
        c->setIsUploading(false);
        return;
    }

    auto remaining = std::make_shared<std::atomic<int>>(uploadsNeeded);

    for (int i = 0; i < MultiLODResult::MAX_LODS; ++i)
    {
        auto& newLOD = mlr.lods[i];
        auto& old = c->getLODData(i);

        /* Free any previous GPU buffers ring-buffer style */
        if (m_renderer &&
            (old.vertexBuffer || old.indexBuffer))
        {
            QueuedChunkDestruction qcd;
            qcd.vb = old.vertexBuffer;  qcd.vbMem = old.vertexMemory;
            qcd.ib = old.indexBuffer;   qcd.ibMem = old.indexMemory;
            m_renderer->enqueueDeferredDestroy(qcd);
        }

        /* Reset current references                                               */
        old.vertexBuffer = old.indexBuffer = VK_NULL_HANDLE;
        old.vertexMemory = old.indexMemory = VK_NULL_HANDLE;
        old.vertexCount = old.indexCount = 0;
        c->setLODGenerated(i, false);
        c->setLODErrorValue(i, 0.f);

        /* Empty LOD – done already                                              */
        if (newLOD.verts.empty() || newLOD.inds.empty())
            continue;

        /* Prepare shared handle container filled by ResourceManager            */
        struct GPU { VkBuffer vb{}, ib{}; VkDeviceMemory vbMem{}, ibMem{}; };
        auto gpu = std::make_shared<GPU>();
        uint32_t vCnt = static_cast<uint32_t>(newLOD.verts.size());
        uint32_t iCnt = static_cast<uint32_t>(newLOD.inds.size());
        float    lodErr =
            (i < static_cast<int>(newLOD.lodErrors.size())) ? newLOD.lodErrors[i] : 0.f;

        /* Kick async upload                                                     */
        m_resourceManager->createChunkBuffersAsync(
            newLOD.verts, newLOD.inds,
            gpu->vb, gpu->vbMem, gpu->ib, gpu->ibMem,
            /* onComplete ----------------------------------------------------- */
            [c, i, gpu, vCnt, iCnt, lodErr, remaining]()
            {
                /* Chunk might have been unloaded in the meantime               */
                if (!c) return;

                auto& out = c->getLODData(i);
                out.vertexBuffer = gpu->vb;  out.vertexMemory = gpu->vbMem;
                out.indexBuffer = gpu->ib;  out.indexMemory = gpu->ibMem;
                out.vertexCount = vCnt;     out.indexCount = iCnt;
                c->setLODErrorValue(i, lodErr);
                c->setLODGenerated(i, true);

                /* If this was the last outstanding upload → mark ready         */
                if (--(*remaining) == 0)
                    c->setIsUploading(false);
            });
    }
}
void VoxelWorld::drainUploadQueue()
{
    if (!m_useMultiLOD) return;

    size_t bytesLeft = m_uploadBudgetBytes;
    int    chunksLeft = m_uploadBudgetChunks;

    while (!m_uploadQueue.empty() && bytesLeft > 0 && chunksLeft > 0)
    {
        PendingUpload pu = std::move(m_uploadQueue.front());
        if (pu.bytes > bytesLeft) break;
        m_uploadQueue.pop_front();

        finalizeMultiLOD(pu.mlr);
#ifdef BENCHMARK_MODE
        ++m_chunksRebuiltLastFrame;
         // may be 0 if not set
#endif
        bytesLeft -= pu.bytes;
        --chunksLeft;
    }
}
void VoxelWorld::scheduleMeshingSingleLOD(Chunk& chunk, const IMesher* base)
{
    std::shared_ptr<Chunk> keepAlive =
        m_chunkManager.getChunk(chunk.worldX(), chunk.worldY(), chunk.worldZ());

    g_threadPool.enqueueTask(
        [this, keepAlive, base]()
        {
            if (!keepAlive) return;

            std::vector<Vertex>   verts;
            std::vector<uint32_t> inds;

            int ox = keepAlive->worldX() * Chunk::SIZE_X;
            int oy = keepAlive->worldY() * Chunk::SIZE_Y;
            int oz = keepAlive->worldZ() * Chunk::SIZE_Z;

            /* ── high-res timer ────────────────────────────────────────── */
            auto t0 = Clock::now();
            bool ok = base->generateMesh(
                *keepAlive,
                keepAlive->worldX(), keepAlive->worldY(), keepAlive->worldZ(),
                verts, inds,
                ox, oy, oz,
                m_chunkManager);
            auto t1 = Clock::now();
            uint32_t us = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

            if (!ok || verts.empty() || inds.empty())
            {
                keepAlive->setIsUploading(false);
                return;
            }

            MeshBuildResult res;
            res.chunkPtr = keepAlive.get();
            res.verts = std::move(verts);
            res.inds = std::move(inds);
            res.cx = keepAlive->worldX();
            res.cy = keepAlive->worldY();
            res.cz = keepAlive->worldZ();
#ifdef BENCHMARK_MODE
            res.meshingUs = us;
            res.morton = encodeMorton21(res.cx, res.cy, res.cz);
#endif
            {
                std::lock_guard<std::mutex> lk(m_singleLodMutex);
                m_pendingMeshResultsSingleLOD.emplace_back(std::move(res));
            }
        },
        TaskType::Meshing, 50);
}
void VoxelWorld::gatherSingleLODResults()
{
    std::vector<MeshBuildResult> local;
    {
        std::lock_guard<std::mutex> lk(m_singleLodMutex);
        if (!m_pendingMeshResultsSingleLOD.empty())
            local.swap(m_pendingMeshResultsSingleLOD);
    }

    for (auto& r : local)
    {
#ifdef BENCHMARK_MODE
        ++m_chunksRebuiltLastFrame;
        m_cpuMeshingMsLastFrame += r.meshingUs * 0.001f;   // µs → ms

        BenchmarkLogger::ChunkLogRow cl;
        cl.frameNumber = s_worldFrameNumber;
        cl.chunkId = r.morton;
        cl.meshingUs = r.meshingUs;
        cl.vertexCount = static_cast<uint32_t>(r.verts.size());
        BenchmarkLogger::get().logChunk(cl);
#endif
        finalizeSingleLODMesh(r);
    }
}

void VoxelWorld::finalizeSingleLODMesh(MeshBuildResult& r)
{
    if (!r.chunkPtr) return;

    uploadMeshToChunkSingleLOD(*r.chunkPtr, r.verts, r.inds);
}

void VoxelWorld::uploadMeshToChunkSingleLOD(
    Chunk& chunk,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds)
{
    // Early‑out if nothing to upload
    if (verts.empty() || inds.empty())
    {
        chunk.setIsUploading(false);
        return;
    }

    // Keep the chunk alive across async copy
    std::shared_ptr<Chunk> keepAlive =
        m_chunkManager.getChunk(chunk.worldX(), chunk.worldY(), chunk.worldZ());
    if (!keepAlive)
    {
        chunk.setIsUploading(false);
        return;
    }

    struct GPUHandles
    {
        VkBuffer       vb = VK_NULL_HANDLE;
        VkDeviceMemory vbMem = VK_NULL_HANDLE;
        VkBuffer       ib = VK_NULL_HANDLE;
        VkDeviceMemory ibMem = VK_NULL_HANDLE;
    };
    auto gpu = std::make_shared<GPUHandles>();

    uint32_t vCount = static_cast<uint32_t>(verts.size());
    uint32_t iCount = static_cast<uint32_t>(inds.size());

    // Kick off async upload; ResourceManager will fill gpu->vb/ib etc.
    m_resourceManager->createChunkBuffersAsync(
        verts, inds,
        gpu->vb, gpu->vbMem, gpu->ib, gpu->ibMem,
        /* onComplete */ [keepAlive, gpu, vCount, iCount]()
        {
            if (!keepAlive) return; // chunk may have been destroyed

            keepAlive->setVertexBuffer(gpu->vb);
            keepAlive->setVertexMemory(gpu->vbMem);
            keepAlive->setIndexBuffer(gpu->ib);
            keepAlive->setIndexMemory(gpu->ibMem);
            keepAlive->setVertexCount(vCount);
            keepAlive->setIndexCount(iCount);
            keepAlive->setIsUploading(false);
        });
}

// ------------------------------------------------------------------------
// getAvgMeshTime => simple statistic
// ------------------------------------------------------------------------
double VoxelWorld::getAvgMeshTime()
{
    if (s_meshCount == 0) return 0.0;
    return s_totalMeshTime / double(s_meshCount);
}

void VoxelWorld::forceRebuildAllChunks()
{
    const auto& map = m_chunkManager.getAllChunks();
    for (auto& kv : map)
    {
        if (kv.second) kv.second->markDirty();
    }
}

void VoxelWorld::setUploadBudget(size_t bytes, int chunks /*=5*/)
{
    /* clamp bytes */
    if (bytes < MIN_UPLOAD_BUDGET_BYTES)      bytes = MIN_UPLOAD_BUDGET_BYTES;
    else if (bytes > MAX_UPLOAD_BUDGET_BYTES) bytes = MAX_UPLOAD_BUDGET_BYTES;

    /* clamp chunk count */
    if (chunks < MIN_UPLOAD_BUDGET_CHUNKS)        chunks = MIN_UPLOAD_BUDGET_CHUNKS;
    else if (chunks > MAX_UPLOAD_BUDGET_CHUNKS)   chunks = MAX_UPLOAD_BUDGET_CHUNKS;

    m_uploadBudgetBytes = bytes;
    m_uploadBudgetChunks = chunks;
}

