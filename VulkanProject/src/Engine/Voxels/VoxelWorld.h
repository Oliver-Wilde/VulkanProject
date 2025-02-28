#pragma once

#include <vulkan/vulkan.h>
#include "ChunkManager.h"
#include "ChunkMesher.h"
#include "Generation/TerrainGenerator.h"
#include <vector>   // for std::vector
#include <cstdint>  // for uint32_t

class VulkanContext;

/**
 * Manages all voxel chunks, from creation/generation to meshing and GPU upload.
 * Now includes multi-threaded generation/meshing logic in the .cpp file.
 */
class VoxelWorld
{
public:
    // Returns the global average meshing time (seconds per chunk)
    static double getAvgMeshTime();

    VoxelWorld(VulkanContext* context);
    ~VoxelWorld();

    // Creates an initial region of chunks (enqueued for generation)
    void initWorld();

    // Loads/unloads chunks around the player, then schedules meshing tasks,
    // then finalizes completed mesh uploads
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    // Access to chunk manager if needed by renderer, etc.
    ChunkManager& getChunkManager() { return m_chunkManager; }

private:
    VulkanContext* m_context = nullptr;
    ChunkManager    m_chunkManager;
    TerrainGenerator m_terrainGenerator;
    ChunkMesher     m_mesher;

    static constexpr int VIEW_DISTANCE = 16;

    // Multi-threaded approach
    // 1) Schedule meshing tasks for dirty chunks
    void scheduleMeshingForDirtyChunks();

    // 2) Poll worker-thread results (mesh data) and finalize on main thread
    void pollMeshBuildResults();

    // (Deprecated in the multi-thread approach)
    // void updateChunkMeshes(); // <-- No longer needed; remove if desired

    // Upload mesh to per-chunk GPU buffers
    void uploadMeshToChunk(
        Chunk& chunk,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds
    );

    // Destroy chunk buffers before re-upload or unload
    void destroyChunkBuffers(Chunk& chunk);

    // Buffer creation/copy
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory
    );

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);

    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);
};
