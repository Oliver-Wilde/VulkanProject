#include "VoxelWorld.h"
#include "Chunk.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Resources/ResourceManager.h"
#include "Engine/Utils/Logger.h"
#include "Engine/Utils/ThreadPool.h"
#include "Meshing/GreedyMesher.h"
#include "Meshing/NaiveMesher.h"
#include "Engine/Graphics/Renderer.h" // for ring-buffer calls

#include <cmath>
#include <stdexcept>
#include <chrono>
#include <mutex>      // for std::lock_guard

extern ThreadPool g_threadPool;

// ------------------------------------------------------------------------
// For mesh timing stats
// ------------------------------------------------------------------------
static double s_totalMeshTime = 0.0;
static int    s_meshCount = 0;

struct MeshBuildResult
{
    Chunk* chunkPtr = nullptr;
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
    int cx = 0, cy = 0, cz = 0;
};

// This container stores completed meshing tasks from background threads
static std::mutex s_resultMutex;
static std::vector<MeshBuildResult> s_pendingMeshResults;

// ------------------------------------------------------------------------
// Constructor / Destructor
// ------------------------------------------------------------------------
VoxelWorld::VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr)
    : m_context(context)
    , m_resourceManager(resourceMgr)
    , m_renderer(nullptr) // default
{
}



VoxelWorld::~VoxelWorld()
{
    // e.g. free chunk buffers
    auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks) {
        Chunk* c = kv.second.get();
        if (c) {
            destroyChunkBuffers(*c);
        }
    }
}

// ------------------------------------------------------------------------
// If you want to dynamically set or reassign the Renderer
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

    int minCy = -2;
    int maxCy = 5;

    for (int cx = -VIEW_DISTANCE; cx <= VIEW_DISTANCE; ++cx)
    {
        for (int cz = -VIEW_DISTANCE; cz <= VIEW_DISTANCE; ++cz)
        {
            for (int cy = minCy; cy <= maxCy; ++cy)
            {
                Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

                // Queue chunk generation in the thread pool
                g_threadPool.enqueueTask([this, newChunk, cx, cy, cz]()
                    {
                        m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);
                        newChunk->markDirty();
                    });
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
    // 1) Identify center chunk coords from player position
    int centerX = static_cast<int>(std::floor(playerPosX / float(Chunk::SIZE_X)));
    int centerZ = static_cast<int>(std::floor(playerPosZ / float(Chunk::SIZE_Z)));

    int minCy = -2;
    int maxCy = 2;

    // ------------------------------------------------------------
    // 2) Gather new chunks to load, then sort them by distance
    // ------------------------------------------------------------
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
                    toLoad.push_back({ cx, cy, cz });
                }
            }
        }
    }

    // Helper lambda to compute distance^2 from center
    auto distanceSq = [&](const ChunkCoord& cc) {
        int dx = cc.x - centerX;
        int dz = cc.z - centerZ;
        return dx * dx + dz * dz;  // no sqrt needed
        };

    // Sort the chunks so nearest ones come first
    std::sort(toLoad.begin(), toLoad.end(), [&](const ChunkCoord& a, const ChunkCoord& b) {
        return distanceSq(a) < distanceSq(b);
        });

    // Now push them in sorted order into m_chunksToLoad
    for (auto& cc : toLoad)
    {
        m_chunksToLoad.push_back(cc);
    }

    // ------------------------------------------------------------
    // 3) Enqueue out-of-range chunks for unloading
    // ------------------------------------------------------------
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
    for (auto& cc : outOfRange)
    {
        m_chunksToUnload.push_back(cc);
    }

    // ------------------------------------------------------------
    // 4) Throttle loading & unloading
    // ------------------------------------------------------------
    constexpr int LOAD_BUDGET = 50;
    constexpr int UNLOAD_BUDGET = 50;

    int loadCount = 0;
    int unloadCount = 0;

    while (!m_chunksToLoad.empty() && loadCount < LOAD_BUDGET)
    {
        ChunkCoord c = m_chunksToLoad.front();
        m_chunksToLoad.pop_front();
        loadOneChunk(c);
        loadCount++;
    }

    while (!m_chunksToUnload.empty() && unloadCount < UNLOAD_BUDGET)
    {
        ChunkCoord c = m_chunksToUnload.front();
        m_chunksToUnload.pop_front();
        unloadOneChunk(c);
        unloadCount++;
    }

    // ------------------------------------------------------------
    // 5) Schedule meshing for dirty chunks, then poll
    // ------------------------------------------------------------
    scheduleMeshingForDirtyChunks();
    pollMeshBuildResults();
}


// ------------------------------------------------------------------------
// loadOneChunk => create chunk, queue generation
// ------------------------------------------------------------------------
void VoxelWorld::loadOneChunk(const ChunkCoord& c)
{
    if (m_chunkManager.hasChunk(c.x, c.y, c.z)) return;

    Logger::Info("loadOneChunk() => scheduling chunk("
        + std::to_string(c.x) + "," + std::to_string(c.y) + "," + std::to_string(c.z) + ")");

    Chunk* newChunk = m_chunkManager.createChunk(c.x, c.y, c.z);

    // Queue generation
    g_threadPool.enqueueTask([this, newChunk, c]()
        {
            m_terrainGenerator.generateChunk(*newChunk, c.x, c.y, c.z);
            newChunk->markDirty();
        });
}

// ------------------------------------------------------------------------
// unloadOneChunk => remove chunk from manager, ring-buffer its buffers
// ------------------------------------------------------------------------
void VoxelWorld::unloadOneChunk(const ChunkCoord& c)
{
    // 1) Grab the chunk if it exists
    Chunk* oldC = m_chunkManager.getChunk(c.x, c.y, c.z);
    if (!oldC) {
        return; // already not present
    }

    // 2) If the chunk is still uploading (being meshed in a background thread),
    //    skip unloading this frame to avoid a race condition.
    if (oldC->isUploading()) {
        return; // We'll try again later
    }

    // 3) Gather the current GPU buffers (if any)
    VkBuffer       vb = oldC->getVertexBuffer();
    VkDeviceMemory vbMem = oldC->getVertexMemory();
    VkBuffer       ib = oldC->getIndexBuffer();
    VkDeviceMemory ibMem = oldC->getIndexMemory();

    // 4) Reset the chunk's pointers (so we don't accidentally use them later)
    oldC->setVertexBuffer(VK_NULL_HANDLE);
    oldC->setVertexMemory(VK_NULL_HANDLE);
    oldC->setIndexBuffer(VK_NULL_HANDLE);
    oldC->setIndexMemory(VK_NULL_HANDLE);

    // 5) Actually remove it from the ChunkManager
    m_chunkManager.removeChunk(c.x, c.y, c.z);

    // 6) If we have a renderer, enqueue these old buffers for ring-buffer
    //    destruction. The Renderer will free them once the GPU is done.
    if (m_renderer && (vb != VK_NULL_HANDLE || ib != VK_NULL_HANDLE)) {
        QueuedChunkDestruction qcd;
        qcd.vb = vb;
        qcd.vbMem = vbMem;
        qcd.ib = ib;
        qcd.ibMem = ibMem;

        m_renderer->enqueueDeferredDestroy(qcd);
    }
}

// ------------------------------------------------------------------------
// scheduleMeshingForDirtyChunks => check for dirty => spawn meshing tasks
// ------------------------------------------------------------------------
void VoxelWorld::scheduleMeshingForDirtyChunks()
{
    const auto& allChunks = m_chunkManager.getAllChunks();

    const IMesher* activeMesher =
        (m_currentMesherType == MesherType::GREEDY)
        ? static_cast<const IMesher*>(&m_greedyMesher)
        : static_cast<const IMesher*>(&m_naiveMesher);

    for (auto& kv : allChunks)
    {
        ChunkCoord coord = kv.first;
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;

        if (chunk->isDirty())
        {
            chunk->clearDirty();
            chunk->setIsUploading(true);

            int offX = coord.x * Chunk::SIZE_X;
            int offY = coord.y * Chunk::SIZE_Y;
            int offZ = coord.z * Chunk::SIZE_Z;

            g_threadPool.enqueueTask([this, chunk, coord, offX, offY, offZ, activeMesher]()
                {
                    auto t0 = std::chrono::high_resolution_clock::now();

                    std::vector<Vertex> verts;
                    std::vector<uint32_t> inds;

                    activeMesher->generateMesh(
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

                    // Fill result
                    MeshBuildResult result;
                    result.chunkPtr = chunk;
                    result.cx = coord.x;
                    result.cy = coord.y;
                    result.cz = coord.z;
                    result.verts = std::move(verts);
                    result.inds = std::move(inds);

                    // Push to global list
                    {
                        std::lock_guard<std::mutex> lk(s_resultMutex);
                        s_pendingMeshResults.push_back(std::move(result));
                    }
                });
        }
    }
}

// ------------------------------------------------------------------------
// pollMeshBuildResults => finalize newly meshed chunks
// ------------------------------------------------------------------------
void VoxelWorld::pollMeshBuildResults()
{
    std::vector<MeshBuildResult> localCopy;
    {
        std::lock_guard<std::mutex> lk(s_resultMutex);
        if (!s_pendingMeshResults.empty()) {
            localCopy.swap(s_pendingMeshResults);
        }
    }

    for (auto& res : localCopy) {
        if (!res.chunkPtr) continue;

        if (!res.verts.empty() && !res.inds.empty())
        {
            Logger::Info("Finalizing chunk mesh for ("
                + std::to_string(res.cx) + ","
                + std::to_string(res.cy) + ","
                + std::to_string(res.cz) + ") => "
                + std::to_string(res.verts.size()) + " verts, "
                + std::to_string(res.inds.size()) + " inds");

            destroyChunkBuffers(*res.chunkPtr);
            uploadMeshToChunk(*res.chunkPtr, res.verts, res.inds);
        }
        else
        {
            // no geometry => free old buffers
            destroyChunkBuffers(*res.chunkPtr);
        }

        res.chunkPtr->setIsUploading(false);
    }
}

// ------------------------------------------------------------------------
// uploadMeshToChunk => create new GPU buffers
// ------------------------------------------------------------------------
void VoxelWorld::uploadMeshToChunk(
    Chunk& chunk,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds
)
{
    // free old
    destroyChunkBuffers(chunk);

    if (!verts.empty() && !inds.empty())
    {
        VkBuffer vb = VK_NULL_HANDLE;
        VkBuffer ib = VK_NULL_HANDLE;
        VkDeviceMemory vbM = VK_NULL_HANDLE;
        VkDeviceMemory ibM = VK_NULL_HANDLE;

        if (m_resourceManager)
        {
            m_resourceManager->createChunkBuffers(verts, inds, vb, vbM, ib, ibM);
        }

        chunk.setVertexBuffer(vb);
        chunk.setVertexMemory(vbM);
        chunk.setIndexBuffer(ib);
        chunk.setIndexMemory(ibM);

        chunk.setVertexCount(static_cast<uint32_t>(verts.size()));
        chunk.setIndexCount(static_cast<uint32_t>(inds.size()));
    }
    else
    {
        chunk.setVertexCount(0);
        chunk.setIndexCount(0);
    }
}

// ------------------------------------------------------------------------
// destroyChunkBuffers => calls ResourceManager to free chunk buffers
// ------------------------------------------------------------------------
void VoxelWorld::destroyChunkBuffers(Chunk& chunk)
{
    if (m_resourceManager)
    {
        m_resourceManager->destroyChunkBuffers(
            chunk.getVertexBuffer(), chunk.getVertexMemory(),
            chunk.getIndexBuffer(), chunk.getIndexMemory()
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
// Unused stubs from original code
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

// ------------------------------------------------------------------------
// getAvgMeshTime => timing for meshing
// ------------------------------------------------------------------------
double VoxelWorld::getAvgMeshTime()
{
    if (s_meshCount == 0) {
        return 0.0;
    }
    return s_totalMeshTime / s_meshCount;
}

