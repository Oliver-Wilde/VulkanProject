#include "VoxelWorld.h"
#include "Chunk.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Utils/Logger.h"
#include <cmath>
#include <stdexcept>

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


    for (int cx = -VIEW_DISTANCE; cx <= VIEW_DISTANCE; ++cx)
    {
        for (int cz = -VIEW_DISTANCE; cz <= VIEW_DISTANCE; ++cz)
        {
            // If you only want one “vertical layer” for now, set cy = 0:
            int cy = 0;

            // 1) Create (or retrieve) the chunk from the manager
            Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);

            // 2) Run your noise-based generator (stone/dirt/grass)
            m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);

            // 3) Mark it dirty so we rebuild the mesh
            newChunk->markDirty();
        }
    }

    // 4) Now that all chunks are filled & dirty, do a one-pass meshing/upload
    updateChunkMeshes();

    Logger::Info("initWorld() => Completed static terrain generation in +/- "
        + std::to_string(VIEW_DISTANCE) + " around (0,0).");
}


void VoxelWorld::destroyChunkBuffers(Chunk& chunk)
{
    VkDevice device = m_context->getDevice();

    // Destroy vertex buffer if valid
    if (chunk.getVertexBuffer() != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, chunk.getVertexBuffer(), nullptr);
        chunk.setVertexBuffer(VK_NULL_HANDLE);
    }
    if (chunk.getVertexMemory() != VK_NULL_HANDLE) {
        vkFreeMemory(device, chunk.getVertexMemory(), nullptr);
        chunk.setVertexMemory(VK_NULL_HANDLE);
    }

    // Destroy index buffer if valid
    if (chunk.getIndexBuffer() != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, chunk.getIndexBuffer(), nullptr);
        chunk.setIndexBuffer(VK_NULL_HANDLE);
    }
    if (chunk.getIndexMemory() != VK_NULL_HANDLE) {
        vkFreeMemory(device, chunk.getIndexMemory(), nullptr);
        chunk.setIndexMemory(VK_NULL_HANDLE);
    }

    // Reset index count
    chunk.setIndexCount(0);
}

void VoxelWorld::updateChunksAroundPlayer(float playerPosX, float playerPosZ)
{
    int centerChunkX = (int)std::floor(playerPosX / (float)Chunk::SIZE_X);
    int centerChunkZ = (int)std::floor(playerPosZ / (float)Chunk::SIZE_Z);

    Logger::Info("Player at (" + std::to_string(playerPosX)
        + ", " + std::to_string(playerPosZ)
        + "), centerChunk = (" + std::to_string(centerChunkX)
        + ", " + std::to_string(centerChunkZ) + ")");

    // 1) Create any needed chunks
    for (int cx = centerChunkX - VIEW_DISTANCE; cx <= centerChunkX + VIEW_DISTANCE; cx++) {
        for (int cz = centerChunkZ - VIEW_DISTANCE; cz <= centerChunkZ + VIEW_DISTANCE; cz++) {
            int cy = 0;
            if (!m_chunkManager.hasChunk(cx, cy, cz)) {
                Logger::Info("Needs chunk at (" + std::to_string(cx) + ","
                    + std::to_string(cy) + ","
                    + std::to_string(cz) + ")");
                Chunk* newChunk = m_chunkManager.createChunk(cx, cy, cz);
                m_terrainGenerator.generateChunk(*newChunk, cx, cy, cz);
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
        m_chunkManager.removeChunk(rc.x, rc.y, rc.z);
    }

    // 3) Re-mesh changed or new chunks
    updateChunkMeshes();
}

void VoxelWorld::updateChunkMeshes()
{
    // Get all chunks from the manager
    const auto& allChunks = m_chunkManager.getAllChunks();

    for (auto& kv : allChunks)
    {
        ChunkCoord coord = kv.first;
        Chunk* chunk = kv.second.get();
        if (!chunk)
            continue;

        // Only rebuild if the chunk is marked dirty (has updated voxel data)
        if (!chunk->isDirty())
            continue;

        // We'll store the new mesh data here
        std::vector<Vertex> verts;
        std::vector<uint32_t> inds;

        // Compute the chunk’s offset in world space
        int offsetX = coord.x * Chunk::SIZE_X;
        int offsetY = coord.y * Chunk::SIZE_Y;
        int offsetZ = coord.z * Chunk::SIZE_Z;

        // === GREEDY MESHING CALL ===
        // Instead of the old naive call, do:
        m_mesher.generateMeshGreedy(
            *chunk,               // which chunk to mesh
            coord.x, coord.y, coord.z,
            verts, inds,
            offsetX, offsetY, offsetZ,
            m_chunkManager        // needed for adjacency checks
        );
        // ===========================

        // Print debug info
        Logger::Info(
            "Greedy meshed chunk ("
            + std::to_string(coord.x) + ", "
            + std::to_string(coord.y) + ", "
            + std::to_string(coord.z) + ") -> "
            + std::to_string(verts.size()) + " verts, "
            + std::to_string(inds.size()) + " indices"
        );

        // Destroy any old GPU buffers on this chunk (to avoid leaks)
        destroyChunkBuffers(*chunk);

        // If we actually produced geometry, upload it
        if (!verts.empty() && !inds.empty()) {
            uploadMeshToChunk(*chunk, verts, inds);
        }

        // Done remeshing => clear the dirty flag
        chunk->clearDirty();
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

    //----------------------------------
    // 1) Create device-local buffers
    //----------------------------------
    // We first create local variables for the newly created buffers/memory:
    VkBuffer       newVB = VK_NULL_HANDLE;
    VkDeviceMemory newVBMem = VK_NULL_HANDLE;

    VkBuffer       newIB = VK_NULL_HANDLE;
    VkDeviceMemory newIBMem = VK_NULL_HANDLE;

    // Vertex buffer
    createBuffer(
        vbSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        /*out*/ newVB,
        /*out*/ newVBMem
    );

    // Index buffer
    createBuffer(
        ibSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        /*out*/ newIB,
        /*out*/ newIBMem
    );

    //----------------------------------
    // 2) Create staging buffers
    //----------------------------------
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

    //----------------------------------
    // 3) Copy CPU data into staging
    //----------------------------------
    {
        void* data;
        vkMapMemory(m_context->getDevice(), stagingVBMem, 0, vbSize, 0, &data);
        memcpy(data, verts.data(), static_cast<size_t>(vbSize));
        vkUnmapMemory(m_context->getDevice(), stagingVBMem);

        vkMapMemory(m_context->getDevice(), stagingIBMem, 0, ibSize, 0, &data);
        memcpy(data, inds.data(), static_cast<size_t>(ibSize));
        vkUnmapMemory(m_context->getDevice(), stagingIBMem);
    }

    //----------------------------------
    // 4) Transfer staging -> device-local
    //----------------------------------
    copyBuffer(stagingVB, newVB, vbSize);
    copyBuffer(stagingIB, newIB, ibSize);

    // Destroy staging
    vkDestroyBuffer(m_context->getDevice(), stagingVB, nullptr);
    vkFreeMemory(m_context->getDevice(), stagingVBMem, nullptr);
    vkDestroyBuffer(m_context->getDevice(), stagingIB, nullptr);
    vkFreeMemory(m_context->getDevice(), stagingIBMem, nullptr);

    //----------------------------------
    // 5) Assign the new buffers to the chunk
    //----------------------------------
    chunk.setVertexBuffer(newVB);
    chunk.setVertexMemory(newVBMem);
    chunk.setIndexBuffer(newIB);
    chunk.setIndexMemory(newIBMem);

    // Store index count for rendering
    chunk.setIndexCount(static_cast<uint32_t>(inds.size()));
    chunk.setVertexCount(static_cast<uint32_t>(verts.size()));
    
}

//--------------------------------------------------
// createBuffer, copyBuffer, findMemoryType
//--------------------------------------------------
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
