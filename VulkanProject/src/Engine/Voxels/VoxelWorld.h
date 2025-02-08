#pragma once

#include <vulkan/vulkan.h>
#include "ChunkManager.h"
#include "ChunkMesher.h"
#include "Generation/TerrainGenerator.h"

class VulkanContext;

class VoxelWorld
{
public:
    VoxelWorld(VulkanContext* context);
    ~VoxelWorld();

    // Creates an initial region of chunks
    void initWorld();

    // Loads/unloads chunks around the player, then re-meshes
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    // If the Renderer needs access to chunk manager:
    ChunkManager& getChunkManager() { return m_chunkManager; }

private:
    VulkanContext* m_context = nullptr;
    ChunkManager     m_chunkManager;
    TerrainGenerator m_terrainGenerator;
    ChunkMesher      m_mesher;

    static constexpr int VIEW_DISTANCE = 8;

    // Re-mesh only dirty chunks
    void updateChunkMeshes();

    // Upload mesh to per-chunk GPU buffers
    void uploadMeshToChunk(Chunk& chunk,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds);

    // Destroy chunk buffers before re-upload or unload
    void destroyChunkBuffers(Chunk& chunk);

    // Buffer creation/copy
    void createBuffer(VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory);

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);
};