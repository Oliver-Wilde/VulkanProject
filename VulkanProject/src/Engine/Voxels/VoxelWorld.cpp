#include "VoxelWorld.h"
#include "Chunk.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Resources/ResourceManager.h"
#include "Engine/Utils/Logger.h"
#include "Engine/Utils/ThreadPool.h"
#include "Meshing/GreedyMesher.h"
#include "Meshing/NaiveMesher.h"
#include "Meshing/LODMesher.h"
#include "Engine/Graphics/Renderer.h" // for ring-buffer calls

#include <cmath>
#include <stdexcept>
#include <chrono>
#include <mutex>
#include <thread>
#include <sstream>

extern ThreadPool g_threadPool;

// ------------------------------------------------------------------------
// For mesh timing stats
// ------------------------------------------------------------------------
static double s_totalMeshTime = 0.0;
static int    s_meshCount = 0;

// SINGLE-LOD leftover from your older code
static std::mutex s_resultMutex;
static std::vector<MeshBuildResult> s_pendingMeshResults;

// ------------------------------------------------------------------------
// VoxelWorld Constructor / Destructor
// ------------------------------------------------------------------------
VoxelWorld::VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr)
    : m_context(context)
    , m_resourceManager(resourceMgr)
    , m_renderer(nullptr)
{
}

VoxelWorld::~VoxelWorld()
{
    auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks) {
        Chunk* c = kv.second.get();
        if (c) {
            // Freed either single-lod or multi-lod, depending on toggle
            // Here we just do single-lod style:
            destroyChunkBuffersSingleLOD(*c);
        }
    }
}

// ------------------------------------------------------------------------
// Set Renderer
// ------------------------------------------------------------------------
void VoxelWorld::setRenderer(Renderer* renderer)
{
    m_renderer = renderer;
}

// ------------------------------------------------------------------------
// initWorld => e.g. queue chunk generation tasks around (0,0)
// ------------------------------------------------------------------------
void VoxelWorld::initWorld()
{
    Logger::Info("initWorld() => generating initial region of chunks.");

    int minCy = 0;
    int maxCy = 0;

    for (int cx = -VIEW_DISTANCE; cx <= VIEW_DISTANCE; ++cx)
    {
        for (int cz = -VIEW_DISTANCE; cz <= VIEW_DISTANCE; ++cz)
        {
            for (int cy = minCy; cy <= maxCy; ++cy)
            {
                Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

                // Queue chunk generation
                int generationPriority = 10;
                g_threadPool.enqueueTask(
                    [this, newChunk, cx, cy, cz]()
                    {
                        m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);
                        newChunk->markDirty();
                    },
                    TaskType::Generation,
                    generationPriority
                );
            }
        }
    }

    Logger::Info("initWorld() => queued vertical chunk tasks around (0,0).");
}

// ------------------------------------------------------------------------
// updateChunksAroundPlayer => throttle loads/unloads + schedule meshing
// ------------------------------------------------------------------------
void VoxelWorld::updateChunksAroundPlayer(float playerPosX, float playerPosZ)
{
    int centerX = int(std::floor(playerPosX / float(Chunk::SIZE_X)));
    int centerZ = int(std::floor(playerPosZ / float(Chunk::SIZE_Z)));

    int minCy = 0;
    int maxCy = 0;

    // 1) Gather new chunks
    std::vector<ChunkCoord> toLoad;
    toLoad.reserve((2 * VIEW_DISTANCE + 1) * (2 * VIEW_DISTANCE + 1) * (maxCy - minCy + 1));

    for (int cx = centerX - VIEW_DISTANCE; cx <= centerX + VIEW_DISTANCE; ++cx)
    {
        for (int cz = centerZ - VIEW_DISTANCE; cz <= centerZ + VIEW_DISTANCE; ++cz)
        {
            for (int cy = minCy; cy <= maxCy; ++cy)
            {
                if (!m_chunkManager.hasChunk(cx, cy, cz))
                {
                    toLoad.push_back({ cx,cy,cz });
                }
            }
        }
    }
    // sort by distance
    auto distSq = [&](const ChunkCoord& cc) {
        int dx = cc.x - centerX;
        int dz = cc.z - centerZ;
        return dx * dx + dz * dz;
        };
    std::sort(toLoad.begin(), toLoad.end(),
        [&](auto& a, auto& b) { return distSq(a) < distSq(b); });
    for (auto& cc : toLoad) {
        m_chunksToLoad.push_back(cc);
    }

    // 2) Out-of-range
    std::vector<ChunkCoord> outOfRange;
    const auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks)
    {
        const ChunkCoord& cc = kv.first;
        int distX = std::abs(cc.x - centerX);
        int distZ = std::abs(cc.z - centerZ);
        if (distX > VIEW_DISTANCE || distZ > VIEW_DISTANCE)
        {
            outOfRange.push_back(cc);
        }
    }
    for (auto& cc : outOfRange) {
        m_chunksToUnload.push_back(cc);
    }

    // 3) Throttle loading/unloading
    constexpr int LOAD_BUDGET = 250;
    constexpr int UNLOAD_BUDGET = 250;

    int loadCount = 0, unloadCount = 0;

    while (!m_chunksToLoad.empty() && loadCount < LOAD_BUDGET) {
        auto c = m_chunksToLoad.front();
        m_chunksToLoad.pop_front();
        loadOneChunk(c);
        loadCount++;
    }
    while (!m_chunksToUnload.empty() && unloadCount < UNLOAD_BUDGET) {
        auto c = m_chunksToUnload.front();
        m_chunksToUnload.pop_front();
        unloadOneChunk(c);
        unloadCount++;
    }

    // 4) schedule + poll
    scheduleMeshingForDirtyChunks();
    pollMeshBuildResults();
}

// ------------------------------------------------------------------------
// loadOneChunk => create chunk, queue generation
// ------------------------------------------------------------------------
void VoxelWorld::loadOneChunk(const ChunkCoord& c)
{
    if (m_chunkManager.hasChunk(c.x, c.y, c.z)) return;

    Logger::Info("loadOneChunk(" + std::to_string(c.x) + ","
        + std::to_string(c.y) + ","
        + std::to_string(c.z) + ")");

    Chunk* newC = m_chunkManager.createChunk(c.x, c.y, c.z);
    int generationPriority = 10;
    g_threadPool.enqueueTask(
        [this, newC, c]()
        {
            m_terrainGenerator.generateChunk(*newC, c.x, c.y, c.z);
            newC->markDirty();
        },
        TaskType::Generation,
        generationPriority
    );
}

// ------------------------------------------------------------------------
// unloadOneChunk => remove chunk from manager, ring-buffer old buffers
// ------------------------------------------------------------------------
void VoxelWorld::unloadOneChunk(const ChunkCoord& c)
{
    Chunk* oldC = m_chunkManager.getChunk(c.x, c.y, c.z);
    if (!oldC) return;
    if (oldC->isUploading()) {
        return;
    }

    // Single-lod ring-buffer destroy
    if (m_renderer) {
        auto vb = oldC->getVertexBuffer();
        auto vbMem = oldC->getVertexMemory();
        auto ib = oldC->getIndexBuffer();
        auto ibMem = oldC->getIndexMemory();

        if (vb != VK_NULL_HANDLE || ib != VK_NULL_HANDLE) {
            QueuedChunkDestruction qcd;
            qcd.vb = vb;
            qcd.vbMem = vbMem;
            qcd.ib = ib;
            qcd.ibMem = ibMem;
            m_renderer->enqueueDeferredDestroy(qcd);
        }
    }

    m_chunkManager.removeChunk(c.x, c.y, c.z);
}

// ------------------------------------------------------------------------
// scheduleMeshingForDirtyChunks => single-lod or multi-lod
// ------------------------------------------------------------------------
void VoxelWorld::scheduleMeshingForDirtyChunks()
{
    // If we want multi-lod => use LODMesher
    // else => single-lod approach
    if (!m_useMultiLOD)
    {
        // Single-lod approach
        const auto& allChunks = m_chunkManager.getAllChunks();
        const IMesher* baseMesher =
            (m_currentMesherType == MesherType::GREEDY)
            ? static_cast<const IMesher*>(&m_greedyMesher)
            : static_cast<const IMesher*>(&m_naiveMesher);

        for (auto& kv : allChunks)
        {
            auto coord = kv.first;
            Chunk* chunk = kv.second.get();
            if (!chunk || !chunk->isDirty()) continue;

            // [NEW] If the chunk is fully EMPTY or fully SOLID, skip meshing
            if (chunk->getState() != Chunk::ChunkState::NORMAL)
            {
                // No need to generate geometry
                chunk->clearDirty();
                continue;
            }

            chunk->clearDirty();
            chunk->setIsUploading(true);

            int offX = coord.x * Chunk::SIZE_X;
            int offY = coord.y * Chunk::SIZE_Y;
            int offZ = coord.z * Chunk::SIZE_Z;

            g_threadPool.enqueueTask(
                [this, chunk, coord, offX, offY, offZ, baseMesher]()
                {
                    auto t0 = std::chrono::high_resolution_clock::now();

                    std::vector<Vertex> verts;
                    std::vector<uint32_t> inds;
                    baseMesher->generateMesh(
                        *chunk,
                        coord.x, coord.y, coord.z,
                        verts, inds,
                        offX, offY, offZ,
                        m_chunkManager
                    );

                    auto t1 = std::chrono::high_resolution_clock::now();
                    double sec = std::chrono::duration<double>(t1 - t0).count();
                    s_totalMeshTime += sec;
                    s_meshCount++;

                    MeshBuildResult res;
                    res.chunkPtr = chunk;
                    res.cx = coord.x;
                    res.cy = coord.y;
                    res.cz = coord.z;
                    res.verts = std::move(verts);
                    res.inds = std::move(inds);

                    std::lock_guard<std::mutex> lk(s_resultMutex);
                    s_pendingMeshResults.push_back(std::move(res));
                },
                TaskType::Meshing,
                50
            );
        }
    }
    else
    {
        // Multi-lod approach
        const auto& allChunks = m_chunkManager.getAllChunks();
        const IMesher* baseMesher =
            (m_currentMesherType == MesherType::GREEDY)
            ? static_cast<const IMesher*>(&m_greedyMesher)
            : static_cast<const IMesher*>(&m_naiveMesher);

        for (auto& kv : allChunks)
        {
            auto coord = kv.first;
            Chunk* chunk = kv.second.get();
            if (!chunk || !chunk->isDirty()) continue;

            // [NEW] Skip if chunk is fully empty or fully solid
            if (chunk->getState() != Chunk::ChunkState::NORMAL)
            {
                chunk->clearDirty();
                continue;
            }

            chunk->clearDirty();
            chunk->setIsUploading(true);

            g_threadPool.enqueueTask(
                [this, chunk, coord, baseMesher]()
                {
                    auto startT = std::chrono::high_resolution_clock::now();
                    MultiLODResult mlr = LODMesher::buildAllLODs(
                        *chunk, coord.x, coord.y, coord.z,
                        baseMesher, m_chunkManager
                    );
                    auto endT = std::chrono::high_resolution_clock::now();

                    double sec = std::chrono::duration<double>(endT - startT).count();
                    s_totalMeshTime += sec;
                    s_meshCount++;

                    std::lock_guard<std::mutex> lock(m_multiLODMutex);
                    m_pendingMultiLODResults.push_back(std::move(mlr));
                },
                TaskType::Meshing,
                50
            );
        }
    }
}

// ------------------------------------------------------------------------
// pollMeshBuildResults => finalize single-lod or multi-lod
// ------------------------------------------------------------------------
void VoxelWorld::pollMeshBuildResults()
{
    // single-lod leftover
    static std::deque<MeshBuildResult> leftoverSingle;
    // multi-lod leftover
    static std::deque<MultiLODResult> leftoverMulti;

    if (!m_useMultiLOD)
    {
        // single-lod
        std::vector<MeshBuildResult> localCopy;
        {
            std::lock_guard<std::mutex> lk(s_resultMutex);
            if (!s_pendingMeshResults.empty()) {
                localCopy.swap(s_pendingMeshResults);
            }
        }
        for (auto& r : localCopy) {
            leftoverSingle.push_back(std::move(r));
        }

        int processed = 0;
        static const int MAX_UPLOADS_PER_FRAME = 100;
        while (!leftoverSingle.empty() && processed < MAX_UPLOADS_PER_FRAME) {
            auto res = std::move(leftoverSingle.front());
            leftoverSingle.pop_front();

            if (!res.chunkPtr) {
                continue;
            }
            if (!res.verts.empty() && !res.inds.empty()) {
                destroyChunkBuffersSingleLOD(*res.chunkPtr);
                uploadMeshToChunkSingleLOD(*res.chunkPtr, res.verts, res.inds);
            }
            else {
                destroyChunkBuffersSingleLOD(*res.chunkPtr);
            }
            res.chunkPtr->setIsUploading(false);
            processed++;
        }
    }
    else
    {
        // multi-lod
        std::vector<MultiLODResult> localList;
        {
            std::lock_guard<std::mutex> lk(m_multiLODMutex);
            if (!m_pendingMultiLODResults.empty()) {
                localList.swap(m_pendingMultiLODResults);
            }
        }
        for (auto& r : localList) {
            leftoverMulti.push_back(std::move(r));
        }

        int processed = 0;
        static const int MAX_UPLOADS_PER_FRAME = 25;
        while (!leftoverMulti.empty() && processed < MAX_UPLOADS_PER_FRAME) {
            auto mlr = std::move(leftoverMulti.front());
            leftoverMulti.pop_front();

            if (!mlr.chunkPtr) {
                continue;
            }
            finalizeMultiLOD(mlr);
            mlr.chunkPtr->setIsUploading(false);
            processed++;
        }
    }
}

// ------------------------------------------------------------------------
// Single-LOD versions of upload/destroy
// ------------------------------------------------------------------------
void VoxelWorld::uploadMeshToChunkSingleLOD(
    Chunk& chunk,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds)
{
    destroyChunkBuffersSingleLOD(chunk);

    if (!verts.empty() && !inds.empty()) {
        VkBuffer vb = VK_NULL_HANDLE;
        VkDeviceMemory vbMem = VK_NULL_HANDLE;
        VkBuffer ib = VK_NULL_HANDLE;
        VkDeviceMemory ibMem = VK_NULL_HANDLE;

        if (m_resourceManager) {
            m_resourceManager->createChunkBuffers(verts, inds, vb, vbMem, ib, ibMem);
        }

        chunk.setVertexBuffer(vb);
        chunk.setVertexMemory(vbMem);
        chunk.setIndexBuffer(ib);
        chunk.setIndexMemory(ibMem);
        chunk.setVertexCount((uint32_t)verts.size());
        chunk.setIndexCount((uint32_t)inds.size());
    }
    else {
        chunk.setVertexCount(0);
        chunk.setIndexCount(0);
    }
}

void VoxelWorld::destroyChunkBuffersSingleLOD(Chunk& chunk)
{
    if (m_resourceManager) {
        m_resourceManager->destroyChunkBuffers(
            chunk.getVertexBuffer(),
            chunk.getVertexMemory(),
            chunk.getIndexBuffer(),
            chunk.getIndexMemory()
        );
    }
    chunk.setVertexBuffer(VK_NULL_HANDLE);
    chunk.setVertexMemory(VK_NULL_HANDLE);
    chunk.setIndexBuffer(VK_NULL_HANDLE);
    chunk.setIndexMemory(VK_NULL_HANDLE);
    chunk.setVertexCount(0);
    chunk.setIndexCount(0);
}

// ------------------------------------------------------------------------
// finalizeMultiLOD => handle each LOD's GPU buffers
// ------------------------------------------------------------------------
void VoxelWorld::finalizeMultiLOD(MultiLODResult& mlr)
{
    Chunk* c = mlr.chunkPtr;
    if (!c) return;

    for (int i = 0; i < MultiLODResult::MAX_LODS; i++)
    {
        auto& newLOD = mlr.lods[i];
        auto& oldData = c->getLODData(i);

        // ring-buffer destroy old
        if (m_renderer) {
            if (oldData.vertexBuffer != VK_NULL_HANDLE ||
                oldData.indexBuffer != VK_NULL_HANDLE)
            {
                QueuedChunkDestruction qcd;
                qcd.vb = oldData.vertexBuffer;
                qcd.vbMem = oldData.vertexMemory;
                qcd.ib = oldData.indexBuffer;
                qcd.ibMem = oldData.indexMemory;
                m_renderer->enqueueDeferredDestroy(qcd);
            }
        }
        oldData.vertexBuffer = VK_NULL_HANDLE;
        oldData.vertexMemory = VK_NULL_HANDLE;
        oldData.indexBuffer = VK_NULL_HANDLE;
        oldData.indexMemory = VK_NULL_HANDLE;
        oldData.vertexCount = 0;
        oldData.indexCount = 0;

        // create new if geometry exists
        if (!newLOD.verts.empty() && !newLOD.inds.empty())
        {
            VkBuffer vb, ib;
            VkDeviceMemory vbM, ibM;
            m_resourceManager->createChunkBuffers(newLOD.verts, newLOD.inds,
                vb, vbM, ib, ibM);

            oldData.vertexBuffer = vb;
            oldData.vertexMemory = vbM;
            oldData.indexBuffer = ib;
            oldData.indexMemory = ibM;
            oldData.vertexCount = (uint32_t)newLOD.verts.size();
            oldData.indexCount = (uint32_t)newLOD.inds.size();
        }
    }
}

// ------------------------------------------------------------------------
// getAvgMeshTime => meshing stats
// ------------------------------------------------------------------------
double VoxelWorld::getAvgMeshTime()
{
    if (s_meshCount == 0) {
        return 0.0;
    }
    return s_totalMeshTime / double(s_meshCount);
}

// ------------------------------------------------------------------------
// Old stubs
// ------------------------------------------------------------------------
void VoxelWorld::createBuffer(VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags,
    VkBuffer&, VkDeviceMemory&)
{
    throw std::runtime_error("createBuffer not used!");
}

void VoxelWorld::copyBuffer(VkBuffer, VkBuffer, VkDeviceSize)
{
    throw std::runtime_error("copyBuffer not used!");
}

uint32_t VoxelWorld::findMemoryType(uint32_t, VkMemoryPropertyFlags)
{
    throw std::runtime_error("findMemoryType not used!");
}
