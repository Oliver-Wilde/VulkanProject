#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <mutex>
#include "ChunkManager.h"
#include "ChunkMesher.h"
#include "Generation/TerrainGenerator.h"

/**
 * For advanced LOD transitions, define a new function pointer or functor
 * if you prefer a separate bridging approach.
 */
class VulkanContext;

/**
 * VoxelWorld orchestrates chunk creation, LOD scheduling, uploading,
 * and now includes partial code for stitching boundaries.
 */
class VoxelWorld
{
public:
    // We keep 3 LOD levels
    static constexpr int LOD_COUNT = 3;

    // Provide read access to meshing stats
    static double getAvgMeshTime();

    VoxelWorld(VulkanContext* context);
    ~VoxelWorld();

    void initWorld();
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    ChunkManager& getChunkManager() { return m_chunkManager; }

private:
    static constexpr int VIEW_DISTANCE = 16;

    VulkanContext* m_context = nullptr;
    ChunkManager    m_chunkManager;
    TerrainGenerator m_terrainGenerator;
    ChunkMesher      m_mesher;

    // Worker thread neighbor updates
    std::mutex              m_neighborMutex;
    std::vector<ChunkCoord> m_pendingNeighborDirty;

    /**
     * Schedules a new mesh build for dirty chunks, respecting the "LOD difference <= 1" rule.
     */
    void scheduleMeshingForDirtyChunks(int centerChunkX, int centerChunkZ);

    /**
     * Poll results from the background meshing tasks & upload to GPU.
     */
    void pollMeshBuildResults();

    /**
     * Upload geometry data to chunk’s LOD buffers (or seam).
     */
    void uploadLODMeshToChunk(
        Chunk& chunk,
        int lodLevel,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds);

    /**
     * Possibly build seam geometry between this chunk and its neighbor, if LOD differs by 1.
     * This is a placeholder. Implementation might be in the mesher or a dedicated class.
     */
    void buildSeamBetweenChunks(Chunk& chunkA, int lodA, Chunk& chunkB, int lodB,
        int faceDirection);

    /**
     * Upload seam geometry to chunk’s seam data for the specified face.
     */
    void uploadSeamMeshToChunk(Chunk& chunk,
        Chunk::SeamDirection seamDir,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds);

    void destroyChunkLOD(Chunk& chunk, int lodLevel);

    /**
     * For seam destruction if needed, or to handle re-build.
     */
    void destroyChunkSeam(Chunk& chunk, Chunk::SeamDirection dir);

    /**
     * Basic buffer creation/copy
     */
    void createBuffer(VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory);

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);
};
