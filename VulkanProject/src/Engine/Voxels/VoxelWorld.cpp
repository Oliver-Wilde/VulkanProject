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



static float averageDequeMs(const std::deque<float>& d)
{
    if (d.empty()) return 0.f;
    return std::accumulate(d.begin(), d.end(), 0.f) /
        static_cast<float>(d.size());
}
// ------------------------------------------------------------------------
// Constructor / Destructor
// ------------------------------------------------------------------------
VoxelWorld::VoxelWorld(VulkanContext* ctx, ResourceManager* rm)
    : m_context(ctx)
    , m_resourceManager(rm)
{
    CpuProfiler::ScopedTimer _t("VoxelWorld::VoxelWorld");
    m_lastFrameTS = Clock::now();           // start frame-timer
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

    /* surrounding columns (single Y‑layer by default) */
    int cyMin = 0, cyMax = 1;
    for (int cx = -VIEW_DISTANCE; cx <= VIEW_DISTANCE; ++cx)
        for (int cz = -VIEW_DISTANCE; cz <= VIEW_DISTANCE; ++cz)
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
    /* ── rolling frame‑time ------------------------------------------------ */
    auto  now = Clock::now();
    float dtMs = std::chrono::duration<float, std::milli>(now - m_lastFrameTS).count();
    m_lastFrameTS = now;

    m_frameTimeMs.push_back(dtMs);
    if (m_frameTimeMs.size() > FRAME_TIME_BUFFER)
        m_frameTimeMs.pop_front();

    float avgMs = averageDequeMs(m_frameTimeMs);

    /* ── adaptive upload‑budget ------------------------------------------- */
    if (m_adjustCooldownFrames > 0)
        --m_adjustCooldownFrames;
    else
    {
        constexpr float UPPER = 17.f;   // >1 frame @60 Hz
        constexpr float LOWER = 10.f;
        constexpr float SHRINK = 0.5f;   // −50 %
        constexpr float GROW = 1.33f;  // +33 %
        constexpr int   COOLDOWN = 60;    // frames ≈1 s @60 Hz

        if (avgMs > UPPER)
        {
            m_uploadBudgetBytes = static_cast<size_t>(m_uploadBudgetBytes * SHRINK);
            m_uploadBudgetChunks = static_cast<int>(m_uploadBudgetChunks * SHRINK);
            m_adjustCooldownFrames = COOLDOWN;
        }
        else if (avgMs < LOWER)
        {
            m_uploadBudgetBytes = static_cast<size_t>(m_uploadBudgetBytes * GROW);
            m_uploadBudgetChunks = static_cast<int>(m_uploadBudgetChunks * GROW);
            m_adjustCooldownFrames = COOLDOWN;
        }

        m_uploadBudgetBytes = std::clamp(m_uploadBudgetBytes,
            MIN_UPLOAD_BUDGET_BYTES,
            MAX_UPLOAD_BUDGET_BYTES);
        m_uploadBudgetChunks = std::clamp(m_uploadBudgetChunks,
            MIN_UPLOAD_BUDGET_CHUNKS,
            MAX_UPLOAD_BUDGET_CHUNKS);
    }

    /* ── periodic streaming update ---------------------------------------- */
    static const int UPDATE_INTERVAL = 10;  // update every 10 frames
    static int frameCounter = 0;
    if (frameCounter++ % UPDATE_INTERVAL == 0)
    {
        CpuProfiler::ScopedTimer t("VoxelWorld::chunkStreaming");

        // Convert player position to chunk coords (X, Z)
        int centerX = static_cast<int>(std::floor(playerPosX / float(Chunk::SIZE_X)));
        int centerZ = static_cast<int>(std::floor(playerPosZ / float(Chunk::SIZE_Z)));

        // Y‑layer (fixed at 0 for now – can extend later)
        float playerPosY = 0.0f;
        if (m_renderer) { /* optional: playerPosY = m_renderer->getCamera().pos.y; */ }
        int centerY = static_cast<int>(std::floor(playerPosY / float(Chunk::SIZE_Y)));

        int verticalRange = 3;
        int minCy = centerY - verticalRange;
        int maxCy = centerY + verticalRange;

        /* 1) chunks to load ------------------------------------------------ */
        std::vector<ChunkCoord> toLoad;
        toLoad.reserve((2 * VIEW_DISTANCE + 1) * (2 * verticalRange + 1) * (2 * VIEW_DISTANCE + 1));

        for (int cx = centerX - VIEW_DISTANCE; cx <= centerX + VIEW_DISTANCE; ++cx)
            for (int cz = centerZ - VIEW_DISTANCE; cz <= centerZ + VIEW_DISTANCE; ++cz)
                for (int cy = minCy; cy <= maxCy; ++cy)
                    if (!m_chunkManager.hasChunk(cx, cy, cz))
                        toLoad.push_back({ cx, cy, cz });

        auto distSq = [&](const ChunkCoord& cc)
            {
                int dx = cc.x - centerX, dy = cc.y - centerY, dz = cc.z - centerZ;
                return (dx * dx + dy * dy + dz * dz);
            };
        std::sort(toLoad.begin(), toLoad.end(), [&](auto& a, auto& b) { return distSq(a) < distSq(b); });
        for (auto& cc : toLoad) m_chunksToLoad.push_back(cc);

        /* 2) chunks to unload --------------------------------------------- */
        const auto& allChunks = m_chunkManager.getAllChunks();
        for (auto& kv : allChunks)
        {
            const ChunkCoord& cc = kv.first;
            int dx = std::abs(cc.x - centerX);
            int dy = std::abs(cc.y - centerY);
            int dz = std::abs(cc.z - centerZ);
            if (dx > VIEW_DISTANCE || dz > VIEW_DISTANCE || dy > verticalRange)
                m_chunksToUnload.push_back(cc);
        }
    }

    /* ── 3) batch loading ------------------------------------------------- */
    const int LOAD_BUDGET = 64;
    int loaded = 0;
    while (!m_chunksToLoad.empty() && loaded < LOAD_BUDGET)
    {
        ChunkCoord c = m_chunksToLoad.front();
        m_chunksToLoad.pop_front();
        loadOneChunk(c);
        ++loaded;
    }

    /* ── 4) batch unloading ---------------------------------------------- */
    const int UNLOAD_BUDGET = 64;
    int unloaded = 0;
    while (!m_chunksToUnload.empty() && unloaded < UNLOAD_BUDGET)
    {
        ChunkCoord c = m_chunksToUnload.front();
        m_chunksToUnload.pop_front();
        unloadOneChunk(c);
        ++unloaded;
    }

    /* ── 5) meshing & GPU uploads ---------------------------------------- */
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
    const IMesher* base = (m_currentMesherType == MesherType::GREEDY)
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

        bytesLeft -= pu.bytes;
        --chunksLeft;
    }
}
void VoxelWorld::scheduleMeshingSingleLOD(Chunk& chunk, const IMesher* base)
{
    // keep chunk alive across threads
    std::shared_ptr<Chunk> keepAlive = m_chunkManager.getChunk(chunk.worldX(), chunk.worldY(), chunk.worldZ());

    g_threadPool.enqueueTask(
        [this, keepAlive, base]()
        {
            if (!keepAlive) return;
            CpuProfiler::ScopedTimer tm("singleLOD mesh");
            std::vector<Vertex> verts; std::vector<uint32_t> inds;
            int ox = keepAlive->worldX() * Chunk::SIZE_X;
            int oy = keepAlive->worldY() * Chunk::SIZE_Y;
            int oz = keepAlive->worldZ() * Chunk::SIZE_Z;

            bool ok = base->generateMesh(*keepAlive,
                keepAlive->worldX(), keepAlive->worldY(), keepAlive->worldZ(),
                verts, inds,
                ox, oy, oz,           // world-space offsets = CORRECT
                m_chunkManager);
            // discard empty/failed meshes
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
        finalizeSingleLODMesh(r);
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