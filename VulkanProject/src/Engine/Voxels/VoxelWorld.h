#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>  // for uint32_t
#include <mutex>    // (new) for std::mutex
#include "ChunkManager.h"
#include "ChunkMesher.h"
#include "Generation/TerrainGenerator.h"

class VulkanContext;

class VoxelWorld
{
public:
    static double getAvgMeshTime();

    VoxelWorld(VulkanContext* context);
    ~VoxelWorld();

    void initWorld();
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    ChunkManager& getChunkManager() { return m_chunkManager; }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // The maximum distance (in chunk coords) from the player
    // ─────────────────────────────────────────────────────────────────────────
    static constexpr int VIEW_DISTANCE = 16;

    // Number of LOD levels
    static constexpr int LOD_COUNT = 3;

    VulkanContext* m_context = nullptr;

    // Our chunk storage
    ChunkManager     m_chunkManager;
    TerrainGenerator m_terrainGenerator;
    ChunkMesher      m_mesher;

    // ─────────────────────────────────────────────────────────────────────────
    // NEW: a place to accumulate "neighbor dirty" requests from worker threads.
    // ─────────────────────────────────────────────────────────────────────────
    std::mutex m_neighborMutex;
    std::vector<ChunkCoord> m_pendingNeighborDirty;

    // Mesh scheduling & finalization
    void scheduleMeshingForDirtyChunks();
    void pollMeshBuildResults();
    void uploadLODMeshToChunk(Chunk& chunk, int lodLevel,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds);

    void destroyChunkLOD(Chunk& chunk, int lodLevel);
    void destroyChunkBuffers(Chunk& chunk);

    void createBuffer(VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory);

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);
};
