#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>  // for uint32_t

#include "ChunkManager.h"
#include "ChunkMesher.h"
#include "Generation/TerrainGenerator.h"

class VulkanContext;

/**
 * Manages all voxel chunks, from creation/generation to multi?LOD meshing
 * and GPU upload.
 */
class VoxelWorld
{
public:
    // Returns the global average meshing time (seconds per chunk).
    static double getAvgMeshTime();

    VoxelWorld(VulkanContext* context);
    ~VoxelWorld();

    // Creates an initial region of chunks (enqueued for generation).
    void initWorld();

    // Loads/unloads chunks around the player, then schedules meshing tasks,
    // then finalizes completed mesh uploads for all LODs.
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    // Access to chunk manager if needed by the renderer, etc.
    ChunkManager& getChunkManager() { return m_chunkManager; }

private:
    // The maximum distance (in chunk coordinates) from player
    static constexpr int VIEW_DISTANCE = 24;

    // We also define how many LOD levels we want to build. 
    // This must match Chunk::MAX_LOD_LEVELS (3).
    static constexpr int LOD_COUNT = 3;

    // Reference to the main Vulkan context (device, queues, etc.)
    VulkanContext* m_context = nullptr;

    // The chunk manager that tracks all loaded chunks.
    ChunkManager     m_chunkManager;

    // Responsible for generating terrain data (voxel IDs).
    TerrainGenerator m_terrainGenerator;

    // Responsible for meshing (building geometry).
    ChunkMesher      m_mesher;

    // 1) For each dirty chunk, schedule a meshing job for each LOD that is dirty.
    void scheduleMeshingForDirtyChunks();

    // 2) Poll worker-thread results (all LOD levels) on the main thread & finalize.
    void pollMeshBuildResults();

    // Upload a mesh (verts/inds) to chunk.lods[lodLevel] buffers
    void uploadLODMeshToChunk(
        Chunk& chunk,
        int lodLevel,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds
    );

    // Destroy the GPU buffers for chunk.lods[lodLevel]
    void destroyChunkLOD(Chunk& chunk, int lodLevel);

    // Destroy old LOD0 buffers (backward-compatible)
    void destroyChunkBuffers(Chunk& chunk);

    // Create a buffer with device-local or staging usage
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory
    );

    // Copy from staging to device-local
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);
};
