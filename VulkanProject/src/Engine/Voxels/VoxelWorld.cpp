#include "VoxelWorld.h"
#include "Chunk.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Utils/Logger.h"
#include <cmath>
#include <stdexcept>
#include <chrono>
#include "Engine/Resources/ResourceManager.h"

// ADD: Include your ThreadPool
#include "Engine/Utils/ThreadPool.h"
#include "Meshing/GreedyMesher.h"    // (We have them included in .h, but it's okay to keep here for definitions)
#include "Meshing/NaiveMesher.h"     // (Likewise)

// We'll assume you have a global or external reference to a thread pool.
// For example, declared somewhere in Application.cpp or a global header:
//    extern ThreadPool g_threadPool;
extern ThreadPool g_threadPool;

// ------------- ADDED FOR TIMING -------------
static double s_totalMeshTime = 0.0;  // accumulates total meshing time (in seconds)
static int    s_meshCount = 0;    // how many chunks have been meshed so far

// We'll provide a static getter at the bottom to return average meshing time
// -------------------------------------------

/**
 * A small struct for passing mesh data from a worker thread back
 * to the main thread for final GPU upload.
 */
struct MeshBuildResult
{
    Chunk* chunkPtr = nullptr;
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
    int cx = 0, cy = 0, cz = 0;
};

// We'll keep a container for "done" mesh results, protected by a mutex.
static std::mutex s_resultMutex;
static std::vector<MeshBuildResult> s_pendingMeshResults;

// --------------------------------------------------------
// The constructor now matches the header exactly:
// --------------------------------------------------------
VoxelWorld::VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr)
    : m_context(context)
    , m_resourceManager(resourceMgr)
{
}

VoxelWorld::~VoxelWorld()
{
    // Destroy GPU buffers for each chunk before device destruction
    auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks) {
        Chunk* c = kv.second.get();
        if (c) {
            destroyChunkBuffers(*c);
        }
    }
}

void VoxelWorld::initWorld()
{
    Logger::Info("initWorld() => Generating multiple vertical layers of chunks.");

    // Example vertical range: from -2 to +5
    int minCy = -2;
    int maxCy = 5;

    // Create chunks for all (cx,cy,cz) in these ranges
    for (int cx = -VIEW_DISTANCE; cx <= VIEW_DISTANCE; ++cx)
    {
        for (int cz = -VIEW_DISTANCE; cz <= VIEW_DISTANCE; ++cz)
        {
            for (int cy = minCy; cy <= maxCy; ++cy)
            {
                Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

                // Enqueue background generation task
                g_threadPool.enqueueTask([this, newChunk, cx, cy, cz]()
                    {
                        m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);
                        newChunk->markDirty();
                    });
            }
        }
    }

    Logger::Info("initWorld() => Queued vertical chunk tasks around (0, 0).");
}

void VoxelWorld::destroyChunkBuffers(Chunk& chunk)
{
    // If we have a ResourceManager, use it to destroy chunk buffers:
    if (m_resourceManager) {
        m_resourceManager->destroyChunkBuffers(
            chunk.getVertexBuffer(), chunk.getVertexMemory(),
            chunk.getIndexBuffer(), chunk.getIndexMemory()
        );
    }
    // Then reset chunk's GPU references:
    chunk.setVertexBuffer(VK_NULL_HANDLE);
    chunk.setVertexMemory(VK_NULL_HANDLE);
    chunk.setIndexBuffer(VK_NULL_HANDLE);
    chunk.setIndexMemory(VK_NULL_HANDLE);
    chunk.setIndexCount(0);
    chunk.setVertexCount(0);
}

void VoxelWorld::updateChunksAroundPlayer(float playerPosX, float playerPosZ)
{
    // Compute the center chunk coords for X and Z
    int centerChunkX = static_cast<int>(std::floor(playerPosX / float(Chunk::SIZE_X)));
    int centerChunkZ = static_cast<int>(std::floor(playerPosZ / float(Chunk::SIZE_Z)));

    // Example vertical range: from -2 to +2
    int minCy = -2;
    int maxCy = 2;

    // 1) Create or queue generation for any needed chunks in X, Z, and Y
    for (int cx = centerChunkX - VIEW_DISTANCE; cx <= centerChunkX + VIEW_DISTANCE; ++cx)
    {
        for (int cz = centerChunkZ - VIEW_DISTANCE; cz <= centerChunkZ + VIEW_DISTANCE; ++cz)
        {
            for (int cy = minCy; cy <= maxCy; ++cy)
            {
                if (!m_chunkManager.hasChunk(cx, cy, cz))
                {
                    Logger::Info("Needs chunk at ("
                        + std::to_string(cx) + ","
                        + std::to_string(cy) + ","
                        + std::to_string(cz) + ")");

                    Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

                    // Enqueue background generation
                    g_threadPool.enqueueTask([this, newChunk, cx, cy, cz]()
                        {
                            m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);
                            newChunk->markDirty();
                        });
                }
            }
        }
    }

    // 2) Unload chunks out of range
    std::vector<ChunkCoord> toRemove;
    const auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks)
    {
        const ChunkCoord& cc = kv.first;
        // Check horizontal distance
        int distX = std::abs(cc.x - centerChunkX);
        int distZ = std::abs(cc.z - centerChunkZ);

        if (distX > VIEW_DISTANCE || distZ > VIEW_DISTANCE)
        {
            toRemove.push_back(cc);
        }
    }

    // Actually remove (and free GPU buffers)
    for (auto& rc : toRemove)
    {
        Chunk* oldC = m_chunkManager.getChunk(rc.x, rc.y, rc.z);
        if (oldC)
        {
            // Only remove the chunk if it's not uploading/meshing:
            if (!oldC->isUploading())
            {
                // Wait for GPU to finish using these buffers
                vkDeviceWaitIdle(m_context->getDevice());
                destroyChunkBuffers(*oldC);
                m_chunkManager.removeChunk(rc.x, rc.y, rc.z);
            }
            // else skip removal this frame; we can remove it later
        }
    }

    // 3) Schedule meshing for any dirty chunks & poll results
    scheduleMeshingForDirtyChunks();
    pollMeshBuildResults();
}

// Instead of synchronous meshing, we schedule tasks on the thread pool
void VoxelWorld::scheduleMeshingForDirtyChunks()
{
    const auto& allChunks = m_chunkManager.getAllChunks();

    // Choose mesher based on current MesherType
    const IMesher* activeMesher = nullptr;
    if (m_currentMesherType == MesherType::GREEDY)
        activeMesher = &m_greedyMesher;
    else
        activeMesher = &m_naiveMesher;

    // For each chunk that is dirty, schedule a worker-thread job
    for (auto& kv : allChunks) {
        ChunkCoord coord = kv.first;
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;
        if (chunk->isDirty()) {
            chunk->clearDirty();
            chunk->setIsUploading(true);

            int offsetX = coord.x * Chunk::SIZE_X;
            int offsetY = coord.y * Chunk::SIZE_Y;
            int offsetZ = coord.z * Chunk::SIZE_Z;

            g_threadPool.enqueueTask([this, chunk, coord, offsetX, offsetY, offsetZ, activeMesher]()
                {
                    auto chunkStart = std::chrono::high_resolution_clock::now();
                    std::vector<Vertex> verts;
                    std::vector<uint32_t> inds;

                    // Generate the mesh
                    activeMesher->generateMesh(
                        *chunk,
                        coord.x, coord.y, coord.z,
                        verts, inds,
                        offsetX, offsetY, offsetZ,
                        m_chunkManager
                    );

                    auto chunkEnd = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double> chunkDurationSec = chunkEnd - chunkStart;
                    s_totalMeshTime += chunkDurationSec.count();
                    s_meshCount++;

                    // Prepare a result object
                    MeshBuildResult result;
                    result.chunkPtr = chunk;
                    result.cx = coord.x;
                    result.cy = coord.y;
                    result.cz = coord.z;
                    result.verts = std::move(verts);
                    result.inds = std::move(inds);

                    Logger::Info("Meshing done for chunk("
                        + std::to_string(result.cx) + ","
                        + std::to_string(result.cy) + ","
                        + std::to_string(result.cz) + ") => "
                        + std::to_string(result.verts.size()) + " verts, "
                        + std::to_string(result.inds.size()) + " inds"
                    );

                    // Put this mesh data in a global vector so main thread can finalize
                    {
                        std::lock_guard<std::mutex> lk(s_resultMutex);
                        s_pendingMeshResults.push_back(std::move(result));
                    }
                });
        }
    }
}

// Poll results from the worker thread and finalize (upload to GPU)
void VoxelWorld::pollMeshBuildResults()
{
    std::vector<MeshBuildResult> localCopy;
    {
        std::lock_guard<std::mutex> lk(s_resultMutex);
        if (!s_pendingMeshResults.empty()) {
            localCopy.swap(s_pendingMeshResults);
            // s_pendingMeshResults is now empty
        }
    }

    // For each result, upload to chunk's GPU buffers
    for (auto& res : localCopy) {
        if (!res.chunkPtr) {
            continue;
        }
        if (!res.verts.empty() && !res.inds.empty()) {
            Logger::Info("Finalizing chunk mesh for ("
                + std::to_string(res.cx) + ","
                + std::to_string(res.cy) + ","
                + std::to_string(res.cz) + ") => "
                + std::to_string(res.verts.size()) + " verts, "
                + std::to_string(res.inds.size()) + " inds"
            );

            // First destroy old buffers if any
            destroyChunkBuffers(*res.chunkPtr);

            // Then upload new geometry
            uploadMeshToChunk(*res.chunkPtr, res.verts, res.inds);
        }
        else {
            // No geometry => destroy old buffers if present
            destroyChunkBuffers(*res.chunkPtr);
        }

        // Mark chunk as no longer uploading => safe to render
        res.chunkPtr->setIsUploading(false);
    }
}

// Actually create new chunk buffers with the ResourceManager
void VoxelWorld::uploadMeshToChunk(
    Chunk& chunk,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds
)
{
    // 1) Destroy old buffers (just in case)
    destroyChunkBuffers(chunk);

    // 2) If there's actual geometry, create new buffers
    if (!verts.empty() && !inds.empty())
    {
        VkBuffer       vb = VK_NULL_HANDLE;
        VkBuffer       ib = VK_NULL_HANDLE;
        VkDeviceMemory vbM = VK_NULL_HANDLE;
        VkDeviceMemory ibM = VK_NULL_HANDLE;

        // If you have a ResourceManager method like createChunkBuffers(...):
        if (m_resourceManager) {
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

// If you used to call createBuffer() and copyBuffer(), that now presumably
// belongs in your ResourceManager. If not, you can remove them or keep them:
void VoxelWorld::createBuffer(VkDeviceSize /*size*/,
    VkBufferUsageFlags /*usage*/,
    VkMemoryPropertyFlags /*props*/,
    VkBuffer& /*buffer*/,
    VkDeviceMemory& /*memory*/)
{
    // If you're not using these now that your ResourceManager handles it,
    // either remove them or implement them. 
    // For now: do nothing or throw if you don't want them called.
    throw std::runtime_error("VoxelWorld::createBuffer() no longer used!");
}

void VoxelWorld::copyBuffer(VkBuffer /*src*/, VkBuffer /*dst*/, VkDeviceSize /*size*/)
{
    throw std::runtime_error("VoxelWorld::copyBuffer() no longer used!");
}

// If you need them, implement them properly here, otherwise remove them
uint32_t VoxelWorld::findMemoryType(uint32_t /*filter*/, VkMemoryPropertyFlags /*props*/)
{
    throw std::runtime_error("VoxelWorld::findMemoryType() no longer used!");
}

// ------------- ADDED GETTER METHOD --------------
double VoxelWorld::getAvgMeshTime()
{
    // Return average meshing time in seconds
    if (s_meshCount == 0) {
        return 0.0;
    }
    return s_totalMeshTime / s_meshCount;
}
// -------------------------------------------------
