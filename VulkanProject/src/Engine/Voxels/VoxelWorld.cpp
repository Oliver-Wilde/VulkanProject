#include "VoxelWorld.h"
#include "Chunk.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Resources/ResourceManager.h"
#include "Engine/Utils/Logger.h"
#include "Engine/Utils/ThreadPool.h"
#include "Meshing/GreedyMesher.h"
#include "Meshing/NaiveMesher.h"
#include "Meshing/LODMesher.h"
#include "Engine/Graphics/Renderer.h" // ring-buffer
#include "../Utils/CpuProfiler.h"     // for CpuProfiler::ScopedTimer

#include <cmath>
#include <stdexcept>
#include <chrono>
#include <mutex>
#include <thread>
#include <sstream>

// Global thread pool
extern ThreadPool g_threadPool;

// Stats for measuring meshing times
static double s_totalMeshTime = 0.0;
static int    s_meshCount = 0;

// Protect multi-lod results
static std::mutex s_resultMutex;
static std::vector<MultiLODResult> s_pendingLODResults;

// -------------------------------------------------------------
// Constructor / Destructor
// -------------------------------------------------------------
VoxelWorld::VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr)
    : m_context(context)
    , m_resourceManager(resourceMgr)
    , m_renderer(nullptr)
{
}

VoxelWorld::~VoxelWorld()
{
    // Clean up GPU buffers for all chunks
    auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks)
    {
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;

        // For each LOD, queue ring-buffer destruction
        for (int lod = 0; lod < Chunk::MAX_LOD_LEVELS; lod++)
        {
            auto& ld = chunk->getLODData(lod);
            if (ld.vertexBuffer || ld.indexBuffer)
            {
                if (m_renderer)
                {
                    QueuedChunkDestruction qcd;
                    qcd.vb = ld.vertexBuffer;
                    qcd.vbMem = ld.vertexMemory;
                    qcd.ib = ld.indexBuffer;
                    qcd.ibMem = ld.indexMemory;
                    m_renderer->enqueueDeferredDestroy(qcd);
                }

                // Nullify them so we never free them again
                ld.vertexBuffer = VK_NULL_HANDLE;
                ld.vertexMemory = VK_NULL_HANDLE;
                ld.indexBuffer = VK_NULL_HANDLE;
                ld.indexMemory = VK_NULL_HANDLE;
                ld.vertexCount = 0;
                ld.indexCount = 0;
            }
        }
    }
}

// -------------------------------------------------------------
void VoxelWorld::setRenderer(Renderer* renderer)
{
    m_renderer = renderer;
}

// -------------------------------------------------------------
void VoxelWorld::initWorld()
{
    Logger::Info("VoxelWorld::initWorld() => generating initial region.");

    int minCy = 0;
    int maxCy = 0;

    for (int cx = -VIEW_DISTANCE; cx <= VIEW_DISTANCE; ++cx)
    {
        for (int cz = -VIEW_DISTANCE; cz <= VIEW_DISTANCE; ++cz)
        {
            for (int cy = minCy; cy <= maxCy; ++cy)
            {
                Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

                // Queue terrain generation
                g_threadPool.enqueueTask(
                    [this, newChunk, cx, cy, cz]()
                    {
                        m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);
                        newChunk->markDirty();
                    },
                    TaskType::Generation,
                    /*priority=*/10
                );
            }
        }
    }

    Logger::Info("initWorld() => queued chunk tasks around origin.");
}

// -------------------------------------------------------------
void VoxelWorld::updateChunksAroundPlayer(float playerPosX, float playerPosZ)
{
    int centerX = int(std::floor(playerPosX / float(Chunk::SIZE_X)));
    int centerZ = int(std::floor(playerPosZ / float(Chunk::SIZE_Z)));

    // 1) Identify new chunks
    std::vector<ChunkCoord> toLoad;
    for (int cx = centerX - VIEW_DISTANCE; cx <= centerX + VIEW_DISTANCE; ++cx)
    {
        for (int cz = centerZ - VIEW_DISTANCE; cz <= centerZ + VIEW_DISTANCE; ++cz)
        {
            if (!m_chunkManager.hasChunk(cx, 0, cz))
            {
                toLoad.push_back({ cx, 0, cz });
            }
        }
    }

    // Sort nearest-first
    auto distSq = [&](const ChunkCoord& cc) {
        int dx = cc.x - centerX;
        int dz = cc.z - centerZ;
        return dx * dx + dz * dz;
        };
    std::sort(toLoad.begin(), toLoad.end(), [&](auto& a, auto& b) {
        return distSq(a) < distSq(b);
        });

    // 2) Strict load budget
    const int LOAD_BUDGET = 5;
    int loadedCount = 0;
    for (auto& c : toLoad)
    {
        if (loadedCount >= LOAD_BUDGET) break;
        loadOneChunk(c);
        loadedCount++;
    }

    // 3) Identify out-of-range => remove
    std::vector<ChunkCoord> toUnload;
    const auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks)
    {
        const ChunkCoord& cc = kv.first;
        if (std::abs(cc.x - centerX) > VIEW_DISTANCE ||
            std::abs(cc.z - centerZ) > VIEW_DISTANCE)
        {
            toUnload.push_back(cc);
        }
    }

    // 4) Strict unload budget
    const int UNLOAD_BUDGET = 5;
    int unloadedCount = 0;
    for (auto& c : toUnload)
    {
        if (unloadedCount >= UNLOAD_BUDGET) break;
        unloadOneChunk(c);
        unloadedCount++;
    }

    // 5) schedule meshing
    scheduleMeshingForDirtyChunks();
    pollMeshBuildResults();
}

// -------------------------------------------------------------
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

// -------------------------------------------------------------
void VoxelWorld::unloadOneChunk(const ChunkCoord& c)
{
    CpuProfiler::ScopedTimer timeUnload("VoxelWorld::unloadOneChunk");

    Chunk* chunk = m_chunkManager.getChunk(c.x, c.y, c.z);
    if (!chunk) return;

    // If chunk is still uploading => re-queue
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

                // Immediately null them out => avoid double-free
                ld.vertexBuffer = VK_NULL_HANDLE;
                ld.vertexMemory = VK_NULL_HANDLE;
                ld.indexBuffer = VK_NULL_HANDLE;
                ld.indexMemory = VK_NULL_HANDLE;
                ld.vertexCount = 0;
                ld.indexCount = 0;
            }
        }
    }

    Logger::Info("unloadOneChunk => removing chunk ("
        + std::to_string(c.x) + ","
        + std::to_string(c.y) + ","
        + std::to_string(c.z) + ")");

    m_chunkManager.removeChunk(c.x, c.y, c.z);
}

// -------------------------------------------------------------
void VoxelWorld::scheduleMeshingForDirtyChunks()
{
    CpuProfiler::ScopedTimer timeSched("VoxelWorld::scheduleMeshing");

    const auto& all = m_chunkManager.getAllChunks();

    const IMesher* baseMesher =
        (m_currentMesherType == MesherType::GREEDY)
        ? static_cast<const IMesher*>(&m_greedyMesher)
        : static_cast<const IMesher*>(&m_naiveMesher);

    for (auto& kv : all)
    {
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;
        if (!chunk->isDirty()) continue;

        // If chunk is uniform => skip
        if (chunk->getState() != Chunk::ChunkState::NORMAL)
        {
            chunk->clearDirty();
            chunk->setIsUploading(false);
            continue;
        }

        chunk->clearDirty();
        chunk->setIsUploading(true);

        g_threadPool.enqueueTask(
            [this, chunk, baseMesher]()
            {
                CpuProfiler::ScopedTimer timeMeshing("VoxelWorld::buildAllLODs");
                auto tBegin = std::chrono::high_resolution_clock::now();

                MultiLODResult mlr =
                    LODMesher::buildAllLODs(
                        *chunk,
                        chunk->worldX(),
                        chunk->worldY(),
                        chunk->worldZ(),
                        baseMesher,
                        m_chunkManager
                    );

                auto tEnd = std::chrono::high_resolution_clock::now();
                double sec = std::chrono::duration<double>(tEnd - tBegin).count();
                s_totalMeshTime += sec;
                s_meshCount++;

                {
                    std::lock_guard<std::mutex> lk(s_resultMutex);
                    s_pendingLODResults.push_back(std::move(mlr));
                }
            },
            TaskType::Meshing,
            /*priority=*/50
        );
    }
}

// -------------------------------------------------------------
// pollMeshBuildResults => read s_pendingLODResults, finalize
// -------------------------------------------------------------
void VoxelWorld::pollMeshBuildResults()
{
    // Grab all results that were finished so far
    std::vector<MultiLODResult> results;
    {
        std::lock_guard<std::mutex> lk(s_resultMutex);
        if (!s_pendingLODResults.empty())
        {
            results.swap(s_pendingLODResults);
        }
    }

    for (auto& mlr : results)
    {
        finalizeMultiLOD(mlr);
    }
}

// -------------------------------------------------------------
void VoxelWorld::finalizeMultiLOD(MultiLODResult& mlr)
{
    Chunk* c = mlr.chunkPtr;
    if (!c) return;

    // For each LOD
    for (int i = 0; i < MultiLODResult::MAX_LODS; i++)
    {
        auto& newLOD = mlr.lods[i];
        auto& oldData = c->getLODData(i);

        // Enqueue the old buffers for destruction
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

        // Immediately set them to null
        oldData.vertexBuffer = VK_NULL_HANDLE;
        oldData.vertexMemory = VK_NULL_HANDLE;
        oldData.indexBuffer = VK_NULL_HANDLE;
        oldData.indexMemory = VK_NULL_HANDLE;
        oldData.vertexCount = 0;
        oldData.indexCount = 0;

        // If we have geometry
        if (!newLOD.verts.empty() && !newLOD.inds.empty())
        {
            VkBuffer vb, ib;
            VkDeviceMemory vbMem, ibMem;
            bool success = m_resourceManager->createChunkBuffers(
                newLOD.verts, newLOD.inds,
                vb, vbMem, ib, ibMem
            );
            if (success)
            {
                oldData.vertexBuffer = vb;
                oldData.vertexMemory = vbMem;
                oldData.indexBuffer = ib;
                oldData.indexMemory = ibMem;
                oldData.vertexCount = static_cast<uint32_t>(newLOD.verts.size());
                oldData.indexCount = static_cast<uint32_t>(newLOD.inds.size());
            }

            // if createChunkBuffers failed, chunk remains with null buffers
            // to avoid partial GPU leaks
        }
        else
        {
            c->setLODGenerated(i, false);
            c->setLODErrorValue(i, 0.f);
            continue;
        }

        // optional: store an error metric
        if (i < static_cast<int>(newLOD.lodErrors.size()))
        {
            c->setLODErrorValue(i, newLOD.lodErrors[i]);
        }
        else
        {
            c->setLODErrorValue(i, 0.f);
        }
        c->setLODGenerated(i, true);
    }

    // Mark chunk as done uploading
    c->setIsUploading(false);
}

//// -------------------------------------------------------------
//bool VoxelWorld::isUsingMultiLOD() const
//{
//    return m_useMultiLOD;
//}
//
//// -------------------------------------------------------------
//void VoxelWorld::setUseMultiLOD(bool enable)
//{
//    m_useMultiLOD = enable;
//}

// -------------------------------------------------------------
//bool VoxelWorld::isChunkVisible(Chunk* chunk) const
//{
//    // Not used if using occlusion pass
//    return true;
//}

//// -------------------------------------------------------------
//void VoxelWorld::setMesherType(MesherType t)
//{
//    m_currentMesherType = t;
//}

//// -------------------------------------------------------------
//VoxelWorld::MesherType VoxelWorld::getMesherType() const
//{
//    return m_currentMesherType;
//}

