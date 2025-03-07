#pragma once

#include <vulkan/vulkan.h>
#include "ChunkManager.h"
// include the meshers
#include "Meshing/NaiveMesher.h"
#include "Meshing/GreedyMesher.h"
#include "Meshing/IMesher.h"
#include "Generation/TerrainGenerator.h"
#include <vector>   // for std::vector
#include <cstdint>  // for uint32_t

// Forward declare ResourceManager if you want to store a pointer only
class ResourceManager;
class VulkanContext;

/**
 * Manages all voxel chunks, from creation/generation to meshing and GPU upload.
 * Now includes multi-threaded generation/meshing logic in the .cpp file.
 */
class VoxelWorld
{
public:

    enum class MesherType { GREEDY, NAIVE };

    void setMesherType(MesherType type) { m_currentMesherType = type; }
    MesherType getMesherType() const { return m_currentMesherType; }

    // Returns the global average meshing time (seconds per chunk)
    static double getAvgMeshTime();

    // -------------------------------------------------------
    // MATCHING SIGNATURE: We accept both VulkanContext* and
    //                     ResourceManager*, because in your .cpp
    //                     you used VoxelWorld(VulkanContext*, ResourceManager*).
    // -------------------------------------------------------
    VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr);

    ~VoxelWorld();

    // Creates an initial region of chunks (enqueued for generation)
    void initWorld();

    // Loads/unloads chunks around the player, then schedules meshing tasks,
    // then finalizes completed mesh uploads
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    // Access to chunk manager if needed by the renderer, etc.
    ChunkManager& getChunkManager() { return m_chunkManager; }

private:
    VulkanContext* m_context = nullptr;
    ResourceManager* m_resourceManager = nullptr;  // we store this pointer

    ChunkManager     m_chunkManager;
    TerrainGenerator m_terrainGenerator;

    static constexpr int VIEW_DISTANCE = 12;

    // Multi-threaded approach
    // 1) Schedule meshing tasks for dirty chunks
    void scheduleMeshingForDirtyChunks();

    // 2) Poll worker-thread results (mesh data) and finalize on main thread
    void pollMeshBuildResults();

    // Upload mesh to per-chunk GPU buffers
    void uploadMeshToChunk(
        Chunk& chunk,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds
    );

    // Destroy chunk buffers before re-upload or unload
    void destroyChunkBuffers(Chunk& chunk);

    // We only keep chunk buffers, not staging, so these are simple wrappers:
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory
    );
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

    // Mesher objects
    GreedyMesher m_greedyMesher;
    NaiveMesher  m_naiveMesher;
    MesherType   m_currentMesherType = MesherType::GREEDY;
};
