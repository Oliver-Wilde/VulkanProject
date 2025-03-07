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

class ResourceManager;
class VulkanContext;

// A small struct for deferring chunk buffer destruction
struct QueuedChunkDestruction
{
    VkBuffer vb = VK_NULL_HANDLE;
    VkDeviceMemory vbMem = VK_NULL_HANDLE;
    VkBuffer ib = VK_NULL_HANDLE;
    VkDeviceMemory ibMem = VK_NULL_HANDLE;
};

class VoxelWorld
{
public:

    enum class MesherType { GREEDY, NAIVE };

    // Switch mesher types
    void setMesherType(MesherType type) { m_currentMesherType = type; }
    MesherType getMesherType() const { return m_currentMesherType; }

    // Returns the global average meshing time (seconds per chunk)
    static double getAvgMeshTime();

    // Matches the .cpp constructor signature
    VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr);
    ~VoxelWorld();

    // Creates an initial region of chunks (enqueued for generation)
    void initWorld();

    // Loads/unloads chunks around the player, schedules meshing tasks,
    // and finalizes completed mesh uploads
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    // Access to chunk manager if needed by the renderer, etc.
    ChunkManager& getChunkManager() { return m_chunkManager; }

    // [NEW] Called by Renderer after waiting on the per-frame fence,
    // so we can safely destroy GPU resources that are no longer in use.
    void flushPendingDestructions();

private:
    VulkanContext* m_context = nullptr;
    ResourceManager* m_resourceManager = nullptr;  // stored pointer

    ChunkManager      m_chunkManager;
    TerrainGenerator  m_terrainGenerator;

    static constexpr int VIEW_DISTANCE = 12;

    // Mesher objects
    GreedyMesher      m_greedyMesher;
    NaiveMesher       m_naiveMesher;
    MesherType        m_currentMesherType = MesherType::GREEDY;

    // 1) Schedules meshing tasks for any dirty chunks
    void scheduleMeshingForDirtyChunks();

    // 2) Poll worker-thread results (mesh data) and finalize on main thread
    void pollMeshBuildResults();

    // 3) Upload mesh to per-chunk GPU buffers
    void uploadMeshToChunk(
        Chunk& chunk,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds
    );

    // 4) Destroy chunk buffers before re-upload or unload
    void destroyChunkBuffers(Chunk& chunk);

    // (Optional) originally in .cpp, no longer used
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory
    );
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

    // [NEW] A container for chunk buffers that are pending safe destruction
    // We'll fill this in updateChunksAroundPlayer(), then free them inside
    // flushPendingDestructions() once fences indicate the GPU is done with them.
    std::vector<QueuedChunkDestruction> m_queuedDestruction;
};
