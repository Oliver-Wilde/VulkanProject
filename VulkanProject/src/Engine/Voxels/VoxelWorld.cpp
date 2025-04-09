#include "Engine/Voxels/VoxelWorld.h"            // or "VoxelWorld.h" if located in the same folder
#include "Engine/Voxels/Chunk.h"
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

// Thread pool declared externally:
extern ThreadPool g_threadPool;

// ------------------------------------------------------------------------
// Stats for measuring meshing times
// ------------------------------------------------------------------------
static double s_totalMeshTime = 0.0;
static int    s_meshCount = 0;

// Protects multi-lod results
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
    // Clean up GPU buffers for all chunks.
    auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks)
    {
        if (Chunk* chunk = kv.second.get())
        {
            // For each LOD, queue ring-buffer destruction
            for (int lod = 0; lod < Chunk::MAX_LOD_LEVELS; lod++)
            {
                auto& lodData = chunk->getLODData(lod);
                if (lodData.vertexBuffer || lodData.indexBuffer)
                {
                    if (m_renderer)
                    {
                        QueuedChunkDestruction qcd;
                        qcd.vb = lodData.vertexBuffer;
                        qcd.vbMem = lodData.vertexMemory;
                        qcd.ib = lodData.indexBuffer;
                        qcd.ibMem = lodData.indexMemory;
                        m_renderer->enqueueDeferredDestroy(qcd);
                    }

                    lodData.vertexBuffer = VK_NULL_HANDLE;
                    lodData.vertexMemory = VK_NULL_HANDLE;
                    lodData.indexBuffer = VK_NULL_HANDLE;
                    lodData.indexMemory = VK_NULL_HANDLE;
                    lodData.vertexCount = 0;
                    lodData.indexCount = 0;
                }
            }
        }
    }
}

// ------------------------------------------------------------------------
// setRenderer
// ------------------------------------------------------------------------
void VoxelWorld::setRenderer(Renderer* renderer)
{
    m_renderer = renderer;
}

// ------------------------------------------------------------------------
// initWorld => generate initial region around (0,0,0) with multiple Y-layers
// ------------------------------------------------------------------------
void VoxelWorld::initWorld()
{
    Logger::Info("VoxelWorld::initWorld() => generating initial region (multiple Y).");

    int centerY = 0;
    int verticalRange = 0; // tweak as desired
    int minCy = centerY - verticalRange;
    int maxCy = centerY + verticalRange;

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
                    /*priority=*/100
                );
            }
        }
    }

    Logger::Info("initWorld() => queued multiple y-layers around (y=0).");
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

    int verticalRange = 2; // how many layers to load above/below camera
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

        // Sort nearest first (3D distance squared)
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
    pollMeshBuildResults();
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

    // Wait if chunk is still uploading
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
        std::to_string(c.x) + ","
        + std::to_string(c.y) + ","
        + std::to_string(c.z) + ")");

    m_chunkManager.removeChunk(c.x, c.y, c.z);
}

// ------------------------------------------------------------------------
// scheduleMeshingForDirtyChunks => multi-lod only
// ------------------------------------------------------------------------
void VoxelWorld::scheduleMeshingForDirtyChunks()
{
    CpuProfiler::ScopedTimer timeSched("VoxelWorld::scheduleMeshing");

    const auto& all = m_chunkManager.getAllChunks();

    // Choose the mesher for LOD=0
    const IMesher* baseMesher =
        (m_currentMesherType == MesherType::GREEDY)
        ? static_cast<const IMesher*>(&m_greedyMesher)
        : static_cast<const IMesher*>(&m_naiveMesher);

    for (auto& kv : all)
    {
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;
        if (!chunk->isDirty()) continue;

        // If chunk is uniform empty or uniform solid => no geometry
        if (chunk->getState() != Chunk::ChunkState::NORMAL)
        {
            chunk->clearDirty();
            continue;
        }

        chunk->clearDirty();
        chunk->setIsUploading(true);

        g_threadPool.enqueueTask(
            [this, chunk, baseMesher]()
            {
                CpuProfiler::ScopedTimer timeMeshing("VoxelWorld::buildAllLODs");
                auto tBegin = std::chrono::high_resolution_clock::now();

                // Build LOD 0..MAX_LOD
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

// ------------------------------------------------------------------------
// pollMeshBuildResults => finalize LOD GPU uploads
// ------------------------------------------------------------------------
void VoxelWorld::pollMeshBuildResults()
{
    static std::deque<MultiLODResult> leftover;

    // Move fresh results into leftover
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
            leftover.push_back(std::move(r));
        }
    }

    // Process a limited number each frame
    static const int MAX_UPLOADS_PER_FRAME = 25;
    int processed = 0;

    while (!leftover.empty() && processed < MAX_UPLOADS_PER_FRAME)
    {
        MultiLODResult mlr = std::move(leftover.front());
        leftover.pop_front();
        finalizeMultiLOD(mlr);
        if (mlr.chunkPtr)
        {
            mlr.chunkPtr->setIsUploading(false);
        }
        processed++;
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
            m_resourceManager->createChunkBuffers(newLOD.verts, newLOD.inds, vb, vbMem, ib, ibMem);

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

// ------------------------------------------------------------------------
// getAvgMeshTime => simple statistic
// ------------------------------------------------------------------------
double VoxelWorld::getAvgMeshTime()
{
    if (s_meshCount == 0) return 0.0;
    return s_totalMeshTime / double(s_meshCount);
}
