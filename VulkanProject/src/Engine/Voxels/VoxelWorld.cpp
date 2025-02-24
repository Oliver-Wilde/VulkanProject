#include "VoxelWorld.h"
#include "Chunk.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Utils/Logger.h"
#include <cmath>
#include <stdexcept>
#include <chrono>

// ADD: Include your ThreadPool
#include "Engine/Utils/ThreadPool.h"

// We'll assume you have a global or external reference to a thread pool.
// For example, declared somewhere in Application.cpp or a global header:
//    extern ThreadPool gThreadPool;
extern ThreadPool g_threadPool;

// ------------- ADDED FOR TIMING -------------
static double s_totalMeshTime = 0.0;  // accumulates total meshing time (in seconds)
static int    s_meshCount = 0;       // how many chunks have been meshed so far

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
static std::mutex                       s_resultMutex;
static std::vector<MeshBuildResult>     s_pendingMeshResults;

VoxelWorld::VoxelWorld(VulkanContext* context)
    : m_context(context)
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
    Logger::Info("initWorld() => Generating a region of procedural chunks around (0,0).");

    // Instead of generating synchronously, enqueue generation tasks in the thread pool.
    for (int cx = -VIEW_DISTANCE; cx <= VIEW_DISTANCE; ++cx)
    {
        for (int cz = -VIEW_DISTANCE; cz <= VIEW_DISTANCE; ++cz)
        {
            int cy = 0;
            Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

            // Enqueue background generation:
            g_threadPool.enqueueTask([this, newChunk, cx, cy, cz]() {
                // 1) Generate chunk data
                m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);

                // 2) Mark chunk as dirty
                newChunk->markDirty();
                });
        }
    }
    
    // We won't call updateChunkMeshes() here yet, because generation is still happening in the background.
    // Instead, we rely on updateChunksAroundPlayer(...) to poll for completed tasks.
    Logger::Info("initWorld() => Queued generation tasks for +/- " + std::to_string(VIEW_DISTANCE) + " around (0,0).");
}

void VoxelWorld::destroyChunkBuffers(Chunk& chunk)
{
    VkDevice device = m_context->getDevice();

    if (chunk.getVertexBuffer() != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, chunk.getVertexBuffer(), nullptr);
        chunk.setVertexBuffer(VK_NULL_HANDLE);
    }
    if (chunk.getVertexMemory() != VK_NULL_HANDLE) {
        vkFreeMemory(device, chunk.getVertexMemory(), nullptr);
        chunk.setVertexMemory(VK_NULL_HANDLE);
    }

    if (chunk.getIndexBuffer() != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, chunk.getIndexBuffer(), nullptr);
        chunk.setIndexBuffer(VK_NULL_HANDLE);
    }
    if (chunk.getIndexMemory() != VK_NULL_HANDLE) {
        vkFreeMemory(device, chunk.getIndexMemory(), nullptr);
        chunk.setIndexMemory(VK_NULL_HANDLE);
    }

    chunk.setIndexCount(0);
}

void VoxelWorld::updateChunksAroundPlayer(float playerPosX, float playerPosZ)
{
    int centerChunkX = (int)std::floor(playerPosX / (float)Chunk::SIZE_X);
    int centerChunkZ = (int)std::floor(playerPosZ / (float)Chunk::SIZE_Z);

    /*Logger::Info("Player at (" + std::to_string(playerPosX)
        + ", " + std::to_string(playerPosZ)
        + "), centerChunk = (" + std::to_string(centerChunkX)
        + ", " + std::to_string(centerChunkZ) + ")");*/

    // 1) Create or queue generation for any needed chunks
    for (int cx = centerChunkX - VIEW_DISTANCE; cx <= centerChunkX + VIEW_DISTANCE; cx++) {
        for (int cz = centerChunkZ - VIEW_DISTANCE; cz <= centerChunkZ + VIEW_DISTANCE; cz++) {
            int cy = 0;
            if (!m_chunkManager.hasChunk(cx, cy, cz)) {
                Logger::Info("Needs chunk at (" + std::to_string(cx) + ","
                    + std::to_string(cy) + ","
                    + std::to_string(cz) + ")");

                Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

                // Enqueue background generation:
                g_threadPool.enqueueTask([this, newChunk, cx, cy, cz]() {
                    m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);
                    newChunk->markDirty();
                    });
            }
        }
    }

    // 2) Unload chunks out of range
    std::vector<ChunkCoord> toRemove;
    const auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks) {
        const ChunkCoord& cc = kv.first;
        if (cc.y != 0) continue; // ignoring multi-layer

        int distX = std::abs(cc.x - centerChunkX);
        int distZ = std::abs(cc.z - centerChunkZ);
        if (distX > VIEW_DISTANCE || distZ > VIEW_DISTANCE) {
            toRemove.push_back(cc);
        }
    }
    for (auto& rc : toRemove) {
        Chunk* oldC = m_chunkManager.getChunk(rc.x, rc.y, rc.z);
        if (oldC) {
            destroyChunkBuffers(*oldC);
        }
        //m_chunkManager.removeChunk(rc.x, rc.y, rc.z);
    }

    // 3) SCHEDULE meshing for changed/new chunks (OFF-MAIN-THREAD),
    //    then poll for completed mesh results and finalize them (ON MAIN THREAD).
    scheduleMeshingForDirtyChunks();
    pollMeshBuildResults();
}

/**
 * Instead of synchronously meshing in updateChunkMeshes(),
 * we schedule meshing tasks for each dirty chunk on the thread pool.
 */
void VoxelWorld::scheduleMeshingForDirtyChunks()
{
    const auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks) {
        ChunkCoord coord = kv.first;
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;

        // If chunk is dirty, schedule a background meshing job.
        if (chunk->isDirty()) {
            chunk->clearDirty(); // so we don't schedule it multiple times

            /*Logger::Info("Enqueuing meshing job for chunk ("
                + std::to_string(coord.x) + ","
                + std::to_string(coord.y) + ","
                + std::to_string(coord.z) + ")");*/

            

            // Copy needed data for the job
            int offsetX = coord.x * Chunk::SIZE_X;
            int offsetY = coord.y * Chunk::SIZE_Y;
            int offsetZ = coord.z * Chunk::SIZE_Z;

            g_threadPool.enqueueTask([this, chunk, coord, offsetX, offsetY, offsetZ]() {
                auto chunkStart = std::chrono::high_resolution_clock::now();

                std::vector<Vertex> verts;
                std::vector<uint32_t> inds;
                // Build geometry
                m_mesher.generateMeshGreedy(
                    *chunk,
                    coord.x, coord.y, coord.z,
                    verts, inds,
                    offsetX, offsetY, offsetZ,
                    m_chunkManager
                );

                auto chunkEnd = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> chunkDurationSec = (chunkEnd - chunkStart);

                // Accumulate global meshing stats
                {
                    // local scope so as not to block a future extension
                    s_totalMeshTime += chunkDurationSec.count();
                    s_meshCount++;
                }

                // Now store the results for the main thread to finalize (upload to GPU).
                MeshBuildResult result;
                result.chunkPtr = chunk;
                result.cx = coord.x;
                result.cy = coord.y;
                result.cz = coord.z;
                result.verts = std::move(verts);
                result.inds = std::move(inds);

                // Add a quick log:
                Logger::Info(
                    "Meshing done for chunk("
                    + std::to_string(result.cx) + ","
                    + std::to_string(result.cy) + ","
                    + std::to_string(result.cz) + ") with "
                    + std::to_string(result.verts.size()) + " verts, "
                    + std::to_string(result.inds.size()) + " inds"
                );

                // Lock and add to the pending results
                {
                    std::lock_guard<std::mutex> lk(s_resultMutex);
                    s_pendingMeshResults.push_back(std::move(result));
                }
                });
        }
    }
}

/**
 * In the main thread, pick up any completed mesh results and do the GPU buffer upload.
 */
void VoxelWorld::pollMeshBuildResults()
{
    std::vector<MeshBuildResult> localCopy;
    {
        std::lock_guard<std::mutex> lk(s_resultMutex);
        if (!s_pendingMeshResults.empty()) {
            localCopy.swap(s_pendingMeshResults);
            // s_pendingMeshResults is now empty, and we have them in localCopy
        }
    }

    // Now localCopy has all the MeshBuildResult items
    for (auto& res : localCopy) {
        // Here 'res' is defined, including res.cx / res.cy / res.cz
        if (res.chunkPtr && !res.verts.empty() && !res.inds.empty()) {

            Logger::Info(
                "Finalizing chunk mesh for ("
                + std::to_string(res.cx) + ","
                + std::to_string(res.cy) + ","
                + std::to_string(res.cz) + ") => "
                + std::to_string(res.verts.size()) + " verts, "
                + std::to_string(res.inds.size()) + " inds"
            );

            destroyChunkBuffers(*res.chunkPtr);
            uploadMeshToChunk(*res.chunkPtr, res.verts, res.inds);
        }
    }
}

void VoxelWorld::uploadMeshToChunk(
    Chunk& chunk,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds
)
{
    VkDeviceSize vbSize = sizeof(Vertex) * verts.size();
    VkDeviceSize ibSize = sizeof(uint32_t) * inds.size();

    VkBuffer       newVB = VK_NULL_HANDLE;
    VkDeviceMemory newVBMem = VK_NULL_HANDLE;

    VkBuffer       newIB = VK_NULL_HANDLE;
    VkDeviceMemory newIBMem = VK_NULL_HANDLE;

    createBuffer(
        vbSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        newVB,
        newVBMem
    );

    createBuffer(
        ibSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        newIB,
        newIBMem
    );

    // Create staging buffers
    VkBuffer stagingVB = VK_NULL_HANDLE, stagingIB = VK_NULL_HANDLE;
    VkDeviceMemory stagingVBMem = VK_NULL_HANDLE, stagingIBMem = VK_NULL_HANDLE;

    createBuffer(
        vbSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingVB,
        stagingVBMem
    );
    createBuffer(
        ibSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingIB,
        stagingIBMem
    );

    // Copy CPU data into staging
    {
        void* data;
        vkMapMemory(m_context->getDevice(), stagingVBMem, 0, vbSize, 0, &data);
        memcpy(data, verts.data(), static_cast<size_t>(vbSize));
        vkUnmapMemory(m_context->getDevice(), stagingVBMem);

        vkMapMemory(m_context->getDevice(), stagingIBMem, 0, ibSize, 0, &data);
        memcpy(data, inds.data(), static_cast<size_t>(ibSize));
        vkUnmapMemory(m_context->getDevice(), stagingIBMem);
    }

    // Transfer staging -> device-local
    copyBuffer(stagingVB, newVB, vbSize);
    copyBuffer(stagingIB, newIB, ibSize);

    // Destroy staging
    vkDestroyBuffer(m_context->getDevice(), stagingVB, nullptr);
    vkFreeMemory(m_context->getDevice(), stagingVBMem, nullptr);
    vkDestroyBuffer(m_context->getDevice(), stagingIB, nullptr);
    vkFreeMemory(m_context->getDevice(), stagingIBMem, nullptr);

    // Assign the new buffers to the chunk
    chunk.setVertexBuffer(newVB);
    chunk.setVertexMemory(newVBMem);
    chunk.setIndexBuffer(newIB);
    chunk.setIndexMemory(newIBMem);

    chunk.setIndexCount(static_cast<uint32_t>(inds.size()));
    chunk.setVertexCount(static_cast<uint32_t>(verts.size()));
}

void VoxelWorld::createBuffer(VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& memory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &bufferInfo, nullptr, &buffer)
        != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_context->getDevice(), buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

    if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &memory)
        != VK_SUCCESS)
    {
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
