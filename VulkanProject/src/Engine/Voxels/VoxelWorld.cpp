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

#include <cmath>
#include <stdexcept>
#include <chrono>
#include <mutex>
#include <thread>
#include <sstream>

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

// ------------------------------------------------------------------------
// Constructor / Destructor
// ------------------------------------------------------------------------
VoxelWorld::VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr)
    : m_context(context)
    , m_resourceManager(resourceMgr)
    , m_renderer(nullptr)
{
}

VoxelWorld::~VoxelWorld()
{
    // ring‑buffer free of every chunk’s GPU buffers
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
                QueuedChunkDestruction q;
                q.vb = ld.vertexBuffer;  q.vbMem = ld.vertexMemory;
                q.ib = ld.indexBuffer;   q.ibMem = ld.indexMemory;
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
    Logger::Info("VoxelWorld::initWorld – generating initial region.");

    /* surrounding columns (single Y‑layer by default) */
    int cyMin = 0, cyMax = 0;
    for (int cx = -VIEW_DISTANCE; cx <= VIEW_DISTANCE; ++cx)
        for (int cz = -VIEW_DISTANCE; cz <= VIEW_DISTANCE; ++cz)
            for (int cy = cyMin; cy <= cyMax; ++cy)
            {
                Chunk* chk = m_chunkManager.createChunk(cx, cy, cz);
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
    // Convert to chunk coords in X, Z
    int centerX = static_cast<int>(std::floor(playerPosX / float(Chunk::SIZE_X)));
    int centerZ = static_cast<int>(std::floor(playerPosZ / float(Chunk::SIZE_Z)));

    // If you have direct camera access, retrieve the actual Y-position
    float playerPosY = 0.0f;
    if (m_renderer)
    {
        // e.g.: playerPosY = m_renderer->getCamera().position.y;
    }
    int centerY = static_cast<int>(std::floor(playerPosY / float(Chunk::SIZE_Y)));

    int verticalRange = 2;
    int minCy = centerY - verticalRange;
    int maxCy = centerY + verticalRange;

    // 1) Identify newly in-range chunks
    {
        std::vector<ChunkCoord> toLoad;
        toLoad.reserve((2 * VIEW_DISTANCE + 1) * (2 * verticalRange + 1)
            * (2 * VIEW_DISTANCE + 1));

        for (int cx = centerX - VIEW_DISTANCE; cx <= centerX + VIEW_DISTANCE; ++cx)
        {
            for (int cz = centerZ - VIEW_DISTANCE; cz <= centerZ + VIEW_DISTANCE; ++cz)
            {
                for (int cy = minCy; cy <= maxCy; ++cy)
                {
                    if (!m_chunkManager.hasChunk(cx, cy, cz))
                    {
                        toLoad.push_back({ cx, cy, cz });
                    }
                }
            }
        }

        // Sort nearest first
        auto distSq = [&](const ChunkCoord& cc)
            {
                int dx = cc.x - centerX;
                int dy = cc.y - centerY;
                int dz = cc.z - centerZ;
                return (dx * dx + dy * dy + dz * dz);
            };
        std::sort(toLoad.begin(), toLoad.end(),
            [&](auto& a, auto& b) { return distSq(a) < distSq(b); });

        for (auto& cc : toLoad)
        {
            m_chunksToLoad.push_back(cc);
        }
    }

    // 2) Identify out-of-range chunks
    {
        const auto& allChunks = m_chunkManager.getAllChunks();
        std::vector<ChunkCoord> outOfRange;
        outOfRange.reserve(allChunks.size());

        for (auto& kv : allChunks)
        {
            const ChunkCoord& cc = kv.first;
            int dx = std::abs(cc.x - centerX);
            int dy = std::abs(cc.y - centerY);
            int dz = std::abs(cc.z - centerZ);

            if (dx > VIEW_DISTANCE || dz > VIEW_DISTANCE || dy > verticalRange)
            {
                outOfRange.push_back(cc);
            }
        }

        for (auto& cc : outOfRange)
        {
            m_chunksToUnload.push_back(cc);
        }
    }

    // 3) Batch loading
    const int LOAD_BUDGET = 250;
    int loadedCount = 0;
    while (!m_chunksToLoad.empty() && loadedCount < LOAD_BUDGET)
    {
        ChunkCoord c = m_chunksToLoad.front();
        m_chunksToLoad.pop_front();
        loadOneChunk(c);
        loadedCount++;
    }

    // 4) Batch unloading
    const int UNLOAD_BUDGET = 250;
    const int UNLOAD_BATCHSIZE = 250;
    int unloadedCount = 0;
    int batchCount = 0;

    while (!m_chunksToUnload.empty() &&
        unloadedCount < UNLOAD_BUDGET &&
        batchCount < UNLOAD_BATCHSIZE)
    {
        ChunkCoord c = m_chunksToUnload.front();
        m_chunksToUnload.pop_front();

        unloadOneChunk(c);
        unloadedCount++;
        batchCount++;
    }

    // 5) schedule LOD building + poll results
    scheduleMeshingForDirtyChunks();
    gatherMesherResults();
    drainUploadQueue();
}

// ------------------------------------------------------------------------
// loadOneChunk => create + queue generation
// ------------------------------------------------------------------------
void VoxelWorld::loadOneChunk(const ChunkCoord& c)
{
    CpuProfiler::ScopedTimer timeLoad("VoxelWorld::loadOneChunk");

    if (m_chunkManager.hasChunk(c.x, c.y, c.z))
    {
        return; // already exists
    }

    Chunk* chunk = m_chunkManager.createChunk(c.x, c.y, c.z);

    // Kick off terrain generation
    g_threadPool.enqueueTask(
        [this, chunk, c]()
        {
            m_terrainGenerator.generateChunk(*chunk, c.x, c.y, c.z);
            chunk->markDirty();
        },
        TaskType::Generation,
        /*priority=*/10
    );
}

// ------------------------------------------------------------------------
// unloadOneChunk => ring-buffer destroy & remove
// ------------------------------------------------------------------------
void VoxelWorld::unloadOneChunk(const ChunkCoord& c)
{
    CpuProfiler::ScopedTimer timeUnload("VoxelWorld::unloadOneChunk");

    Chunk* chunk = m_chunkManager.getChunk(c.x, c.y, c.z);
    if (!chunk)
    {
        return; // not existing
    }

    // If chunk is still uploading
    if (chunk->isUploading())
    {
        Logger::Info("unloadOneChunk => chunk uploading, re-queue: "
            + std::to_string(c.x) + ","
            + std::to_string(c.y) + ","
            + std::to_string(c.z));
        m_chunksToUnload.push_back(c);
        return;
    }

    // Destroy all LOD GPU buffers ring-buffer style
    if (m_renderer)
    {
        for (int lod = 0; lod < Chunk::MAX_LOD_LEVELS; lod++)
        {
            auto& ld = chunk->getLODData(lod);
            if (ld.vertexBuffer || ld.indexBuffer)
            {
                QueuedChunkDestruction qcd;
                qcd.vb = ld.vertexBuffer;
                qcd.vbMem = ld.vertexMemory;
                qcd.ib = ld.indexBuffer;
                qcd.ibMem = ld.indexMemory;
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

// ------------------------------------------------------------------------
// scheduleMeshingForDirtyChunks => multi-lod only
// ------------------------------------------------------------------------
void VoxelWorld::scheduleMeshingForDirtyChunks()
{
    CpuProfiler::ScopedTimer t("VoxelWorld::scheduleMeshing");

    const auto& all = m_chunkManager.getAllChunks();
    const IMesher* base =
        (m_currentMesherType == MesherType::GREEDY)
        ? static_cast<const IMesher*>(&m_greedyMesher)
        : static_cast<const IMesher*>(&m_naiveMesher);

    for (auto& kv : all)
    {
        Chunk* c = kv.second.get();
        if (!c || !c->isDirty()) continue;

        if (c->getState() == Chunk::ChunkState::NORMAL)
            c->recalcFilledBounds();

        if (c->getState() != Chunk::ChunkState::NORMAL)
        {
            c->clearDirty();
            continue;
        }

        c->clearDirty();
        c->setIsUploading(true);

        g_threadPool.enqueueTask([this, c, base]()
            {
                CpuProfiler::ScopedTimer tm("LOD build");

                MultiLODResult mlr = LODMesher::buildAllLODs(
                    *c, c->worldX(), c->worldY(), c->worldZ(),
                    base, m_chunkManager);

                {
                    std::lock_guard<std::mutex> lk(s_resultMutex);
                    s_pendingLODResults.emplace_back(std::move(mlr));
                }
            }, TaskType::Meshing, 50);
    }
}

// ------------------------------------------------------------------------
// pollMeshBuildResults => finalize LOD GPU uploads
// ------------------------------------------------------------------------
void VoxelWorld::gatherMesherResults()
{
    std::vector<MultiLODResult> local;
    {
        std::lock_guard<std::mutex> lk(s_resultMutex);
        if (!s_pendingLODResults.empty())
        {
            local.swap(s_pendingLODResults);
        }
    }
    for (auto& r : local)
    {
        size_t bytes = 0;
        for (int i = 0; i < MultiLODResult::MAX_LODS; ++i)
        {
            bytes += r.lods[i].verts.size() * sizeof(Vertex);
            bytes += r.lods[i].inds.size() * sizeof(uint32_t);
        }

        m_uploadQueue.push_back({ std::move(r), bytes, 0 /*hash placeholder*/ });
    }
}

// ------------------------------------------------------------------------
// finalizeMultiLOD => put LOD geometry on the GPU
// ------------------------------------------------------------------------
void VoxelWorld::finalizeMultiLOD(MultiLODResult& mlr)
{
    Chunk* c = mlr.chunkPtr;
    if (!c) return;

    for (int i = 0; i < MultiLODResult::MAX_LODS; i++)
    {
        auto& newLOD = mlr.lods[i];
        auto& oldData = c->getLODData(i);

        // ring-buffer free old geometry
        if (m_renderer &&
            (oldData.vertexBuffer != VK_NULL_HANDLE ||
                oldData.indexBuffer != VK_NULL_HANDLE))
        {
            QueuedChunkDestruction qcd;
            qcd.vb = oldData.vertexBuffer;
            qcd.vbMem = oldData.vertexMemory;
            qcd.ib = oldData.indexBuffer;
            qcd.ibMem = oldData.indexMemory;
            m_renderer->enqueueDeferredDestroy(qcd);
        }

        // Reset references
        oldData.vertexBuffer = VK_NULL_HANDLE;
        oldData.vertexMemory = VK_NULL_HANDLE;
        oldData.indexBuffer = VK_NULL_HANDLE;
        oldData.indexMemory = VK_NULL_HANDLE;
        oldData.vertexCount = 0;
        oldData.indexCount = 0;

        // If we have geometry at LOD i
        if (!newLOD.verts.empty() && !newLOD.inds.empty())
        {
            VkBuffer vb, ib;
            VkDeviceMemory vbMem, ibMem;
            m_resourceManager->createChunkBuffers(
                newLOD.verts, newLOD.inds,
                vb, vbMem, ib, ibMem
            );

            oldData.vertexBuffer = vb;
            oldData.vertexMemory = vbMem;
            oldData.indexBuffer = ib;
            oldData.indexMemory = ibMem;
            oldData.vertexCount = static_cast<uint32_t>(newLOD.verts.size());
            oldData.indexCount = static_cast<uint32_t>(newLOD.inds.size());

            // Optional: store an error metric if present
            if (i < static_cast<int>(newLOD.lodErrors.size()))
            {
                float eVal = newLOD.lodErrors[i];
                c->setLODErrorValue(i, eVal);
            }
            else
            {
                c->setLODErrorValue(i, 0.f);
            }
            c->setLODGenerated(i, true);
        }
        else
        {
            c->setLODGenerated(i, false);
            c->setLODErrorValue(i, 0.f);
        }
    }
}

void VoxelWorld::drainUploadQueue()
{
    size_t bytesLeft = m_uploadBudgetBytes;
    int    chunksLeft = m_uploadBudgetChunks;

    while (!m_uploadQueue.empty() && bytesLeft > 0 && chunksLeft > 0)
    {
        PendingUpload pu = std::move(m_uploadQueue.front());
        if (pu.bytes > bytesLeft) break;                 // stop, byte budget done

        m_uploadQueue.pop_front();

        finalizeMultiLOD(pu.mlr);                        // synchronous for now
        if (pu.mlr.chunkPtr) pu.mlr.chunkPtr->setIsUploading(false);

        bytesLeft -= pu.bytes;
        --chunksLeft;
    }
}

void VoxelWorld::uploadMeshToChunkSingleLOD(
    Chunk& chunk,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds)
{
    // Mark busy so Renderer / unload logic knows to skip
    chunk.setIsUploading(true);

    /* placeholders that will receive GPU handles */
    VkBuffer       vb = VK_NULL_HANDLE, ib = VK_NULL_HANDLE;
    VkDeviceMemory vbMem = VK_NULL_HANDLE, ibMem = VK_NULL_HANDLE;

    /* capture counts for later */
    uint32_t vCount = static_cast<uint32_t>(verts.size());
    uint32_t iCount = static_cast<uint32_t>(inds.size());

    /* schedule non‑blocking upload */
    m_resourceManager->createChunkBuffersAsync(
        verts, inds,
        vb, vbMem, ib, ibMem,
        /* onComplete = */ [c = &chunk, vb, vbMem, ib, ibMem, vCount, iCount]()
        {
            // IMPORTANT: guard against chunk being destroyed mid‑flight
            if (!c || c->getState() == Chunk::ChunkState::EMPTY) return;

            c->setVertexBuffer(vb);
            c->setVertexMemory(vbMem);
            c->setIndexBuffer(ib);
            c->setIndexMemory(ibMem);
            c->setVertexCount(vCount);
            c->setIndexCount(iCount);

            c->setIsUploading(false);
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

