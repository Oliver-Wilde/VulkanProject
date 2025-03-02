#include "VoxelWorld.h"
#include "Chunk.h"
#include <cmath>
#include <stdexcept>
#include <chrono>
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Utils/Logger.h"

// We'll assume you have a global thread pool declared somewhere
#include "Engine/Utils/ThreadPool.h"
#include "LODDownsampler.h"
extern ThreadPool g_threadPool;

// For timing stats
static double s_totalMeshTime = 0.0;
static int    s_meshCount = 0;

// We'll track LOD results using a separate struct
struct LODMeshBuildResult
{
    Chunk* chunkPtr = nullptr;
    int    cx = 0, cy = 0, cz = 0;
    int    lodLevel = 0;
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
};

static std::mutex s_resultMutexLOD;
static std::vector<LODMeshBuildResult> s_pendingLODResults;

// ─────────────────────────────────────────────────────────────────────────
// A local helper for marking neighbors (no changes needed):
// ─────────────────────────────────────────────────────────────────────────
static void markNeighborsDirty(ChunkManager& manager, int cx, int cy, int cz)
{
    static const int offsets[6][3] = {
        { 1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
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
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// VoxelWorld Implementation
// ─────────────────────────────────────────────────────────────────────────
VoxelWorld::VoxelWorld(VulkanContext* context)
    : m_context(context)
{
}

VoxelWorld::~VoxelWorld()
{
    auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks) {
        Chunk* c = kv.second.get();
        if (c) {
            for (int L = 0; L < LOD_COUNT; L++) {
                destroyChunkLOD(*c, L);
            }
        }
    }
}

void VoxelWorld::initWorld()
{
    Logger::Info("initWorld() => Generating a region of procedural chunks around (0,0).");

    for (int cx = -VIEW_DISTANCE; cx <= VIEW_DISTANCE; ++cx)
    {
        for (int cz = -VIEW_DISTANCE; cz <= VIEW_DISTANCE; ++cz)
        {
            int cy = 0;
            Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

            // ─────────────────────────────────────────────────────────────────
            // BACKGROUND GENERATION, but do NOT call markNeighborsDirty here.
            // Instead, we stash the chunk coords for the main thread to handle.
            // ─────────────────────────────────────────────────────────────────
            g_threadPool.enqueueTask([this, cx, cy, cz, newChunk]()
                {
                    m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);
                    newChunk->markAllLODsDirty();

                    // Instead of calling markNeighborsDirty directly:
                    {
                        std::lock_guard<std::mutex> lock(m_neighborMutex);
                        m_pendingNeighborDirty.emplace_back(cx, cy, cz);
                    }
                });
        }
    }
    Logger::Info("initWorld() => Queued generation tasks for +/- "
        + std::to_string(VIEW_DISTANCE) + " around (0,0).");
}

/**
 * Removes GPU buffers for chunk.lods[L].
 */
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

void VoxelWorld::updateChunksAroundPlayer(float playerPosX, float playerPosZ)
{
    int centerChunkX = (int)std::floor(playerPosX / (float)Chunk::SIZE_X);
    int centerChunkZ = (int)std::floor(playerPosZ / (float)Chunk::SIZE_Z);

    // 1) Create or queue generation for needed chunks
    for (int cx = centerChunkX - VIEW_DISTANCE; cx <= centerChunkX + VIEW_DISTANCE; cx++)
    {
        for (int cz = centerChunkZ - VIEW_DISTANCE; cz <= centerChunkZ + VIEW_DISTANCE; cz++)
        {
            int cy = 0;
            if (!m_chunkManager.hasChunk(cx, cy, cz))
            {
                Logger::Info("Needs chunk at (" + std::to_string(cx) + ","
                    + std::to_string(cy) + "," + std::to_string(cz) + ")");
                Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

                g_threadPool.enqueueTask([this, cx, cy, cz, newChunk]()
                    {
                        m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);
                        newChunk->markAllLODsDirty();

                        // Again, do NOT call markNeighborsDirty here:
                        std::lock_guard<std::mutex> lock(m_neighborMutex);
                        m_pendingNeighborDirty.emplace_back(cx, cy, cz);
                    });
            }
        }
    }

    // 2) Unload chunks out of range
    // (unchanged from your code)
    {
        std::vector<ChunkCoord> toRemove;
        const auto& allChunks = m_chunkManager.getAllChunks();
        for (auto& kv : allChunks) {
            const ChunkCoord& cc = kv.first;
            if (cc.y != 0) continue;

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
                m_chunkManager.removeChunk(rc.x, rc.y, rc.z);
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────
    // [NEW] Mark any neighbors for newly generated chunks on the main thread
    // ─────────────────────────────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lock(m_neighborMutex);
        for (auto& cCoord : m_pendingNeighborDirty)
        {
            // Now we can safely call markNeighborsDirty on the main thread
            markNeighborsDirty(m_chunkManager, cCoord.x, cCoord.y, cCoord.z);
        }
        m_pendingNeighborDirty.clear();
    }

    // 3) Schedule meshing for dirty chunks, then poll results
    scheduleMeshingForDirtyChunks();
    pollMeshBuildResults();
}

/**
 * For each chunk, if LOD i is dirty => schedule a job that:
 *   1) Possibly downsample the chunk’s data for LOD i
 *   2) Build the mesh from that array (or from chunk for LOD0)
 *   3) Store results in s_pendingLODResults
 */
void VoxelWorld::scheduleMeshingForDirtyChunks()
{
    const auto& allChunks = m_chunkManager.getAllChunks();
    for (auto& kv : allChunks) {
        ChunkCoord coord = kv.first;
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;

        // If chunk is already uploading, skip
        if (chunk->isUploading()) {
            continue;
        }

        // Check if ANY LOD is dirty
        bool anyLODDirty = false;
        for (int L = 0; L < LOD_COUNT; L++) {
            if (chunk->isLODDirty(L)) {
                anyLODDirty = true;
                break;
            }
        }
        if (!anyLODDirty) {
            continue;
        }

        // Mark chunk as uploading
        chunk->setIsUploading(true);

        // For reference in meshing
        int offsetX = coord.x * Chunk::SIZE_X;
        int offsetY = coord.y * Chunk::SIZE_Y;
        int offsetZ = coord.z * Chunk::SIZE_Z;

        // Enqueue a job that handles all dirty LODs
        g_threadPool.enqueueTask([this, chunk, coord, offsetX, offsetY, offsetZ]()
            {
                auto chunkStart = std::chrono::high_resolution_clock::now();

                std::vector<LODMeshBuildResult> localResults;

                for (int L = 0; L < LOD_COUNT; L++)
                {
                    if (!chunk->isLODDirty(L)) {
                        continue; // skip if not dirty
                    }
                    chunk->clearLODDirty(L);

                    std::vector<Vertex> verts;
                    std::vector<uint32_t> inds;

                    if (L == 0)
                    {
                        // LOD0 => normal meshing (greedy + boundary merging)
                        m_mesher.generateMeshGreedy(*chunk,
                            coord.x, coord.y, coord.z,
                            verts, inds,
                            offsetX, offsetY, offsetZ,
                            m_chunkManager);
                    }
                    else
                    {
                        // LOD>0 => downsample => build from smaller array
                        const std::vector<int>& fullData = chunk->getBlocks();

                        // Downsample
                        std::vector<int> dsData = downsampleVoxelData(
                            fullData,
                            Chunk::SIZE_X,
                            Chunk::SIZE_Y,
                            Chunk::SIZE_Z,
                            L
                        );

                        int dsX = Chunk::SIZE_X >> L;
                        int dsY = Chunk::SIZE_Y >> L;
                        int dsZ = Chunk::SIZE_Z >> L;

                        // Build from that array (greedy or naive at LOD)
                        // This approach doesn't consider external neighbors at LOD.
                        m_mesher.generateMeshFromArray(dsData,
                            dsX, dsY, dsZ,
                            offsetX, offsetY, offsetZ,
                            verts, inds,
                            true /*useGreedy*/);
                    }

                    LODMeshBuildResult res;
                    res.chunkPtr = chunk;
                    res.cx = coord.x;
                    res.cy = coord.y;
                    res.cz = coord.z;
                    res.lodLevel = L;
                    res.verts = std::move(verts);
                    res.inds = std::move(inds);
                    localResults.push_back(std::move(res));
                }

                auto chunkEnd = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> chunkDurSec = (chunkEnd - chunkStart);
                {
                    s_totalMeshTime += chunkDurSec.count();
                    s_meshCount++;
                }

                // Lock & push all results to the global queue
                {
                    std::lock_guard<std::mutex> lk(s_resultMutexLOD);
                    for (auto& r : localResults) {
                        s_pendingLODResults.push_back(std::move(r));
                    }
                }
            });
    }
}

void VoxelWorld::pollMeshBuildResults()
{
    std::vector<LODMeshBuildResult> localCopy;
    {
        std::lock_guard<std::mutex> lk(s_resultMutexLOD);
        if (!s_pendingLODResults.empty()) {
            localCopy.swap(s_pendingLODResults);
        }
    }

    // For each result, upload
    for (auto& res : localCopy) {
        if (!res.chunkPtr) continue;

        Chunk* c = res.chunkPtr;
        if (!res.verts.empty() && !res.inds.empty())
        {
            Logger::Info("Finalizing LOD" + std::to_string(res.lodLevel)
                + " for chunk(" + std::to_string(res.cx)
                + "," + std::to_string(res.cy)
                + "," + std::to_string(res.cz) + ") => "
                + std::to_string(res.verts.size()) + " verts, "
                + std::to_string(res.inds.size()) + " inds");

            // Destroy old buffers for that LOD
            destroyChunkLOD(*c, res.lodLevel);

            // Upload new geometry
            uploadLODMeshToChunk(*c, res.lodLevel, res.verts, res.inds);
            c->getLODData(res.lodLevel).valid = true;
        }
        else {
            // If no geometry, destroy old buffers for that LOD
            destroyChunkLOD(*c, res.lodLevel);
        }
        // If no more LODs are uploading, we can set chunk as not uploading.
        // For simplicity, we assume only one job per chunk was enqueued.
        c->setIsUploading(false);
    }
}

/**
 * Actually uploads the vertex/index data for a specific LOD to the GPU.
 */
void VoxelWorld::uploadLODMeshToChunk(
    Chunk& chunk,
    int lodLevel,
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds)
{
    VkDeviceSize vbSize = sizeof(Vertex) * verts.size();
    VkDeviceSize ibSize = sizeof(uint32_t) * inds.size();

    VkBuffer       newVB = VK_NULL_HANDLE;
    VkDeviceMemory newVBMem = VK_NULL_HANDLE;
    VkBuffer       newIB = VK_NULL_HANDLE;
    VkDeviceMemory newIBMem = VK_NULL_HANDLE;

    // 1) Create device-local buffers
    createBuffer(vbSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        newVB, newVBMem);
    createBuffer(ibSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        newIB, newIBMem);

    // 2) Create staging
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

    // 3) Copy CPU data into staging
    {
        void* data;
        vkMapMemory(m_context->getDevice(), stagingVBMem, 0, vbSize, 0, &data);
        memcpy(data, verts.data(), (size_t)vbSize);
        vkUnmapMemory(m_context->getDevice(), stagingVBMem);

        vkMapMemory(m_context->getDevice(), stagingIBMem, 0, ibSize, 0, &data);
        memcpy(data, inds.data(), (size_t)ibSize);
        vkUnmapMemory(m_context->getDevice(), stagingIBMem);
    }

    // 4) Transfer staging => device local
    copyBuffer(stagingVB, newVB, vbSize);
    copyBuffer(stagingIB, newIB, ibSize);

    // 5) Destroy staging
    VkDevice device = m_context->getDevice();
    vkDestroyBuffer(device, stagingVB, nullptr);
    vkFreeMemory(device, stagingVBMem, nullptr);
    vkDestroyBuffer(device, stagingIB, nullptr);
    vkFreeMemory(device, stagingIBMem, nullptr);

    // 6) Assign new buffers to chunk
    auto& lodData = chunk.getLODData(lodLevel);
    lodData.vertexBuffer = newVB;
    lodData.vertexMemory = newVBMem;
    lodData.indexBuffer = newIB;
    lodData.indexMemory = newIBMem;
    lodData.vertexCount = (uint32_t)verts.size();
    lodData.indexCount = (uint32_t)inds.size();
    lodData.valid = true;
}

void VoxelWorld::createBuffer(
    VkDeviceSize size,
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

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((filter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

double VoxelWorld::getAvgMeshTime()
{
    if (s_meshCount == 0) {
        return 0.0;
    }
    return s_totalMeshTime / s_meshCount;
}
