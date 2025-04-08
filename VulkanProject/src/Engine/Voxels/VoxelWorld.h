#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <deque>
#include <cstdint>
#include <mutex>

#include "ChunkManager.h"
#include "Meshing/NaiveMesher.h"
#include "Meshing/GreedyMesher.h"
#include "Meshing/IMesher.h"
#include "Generation/TerrainGenerator.h"
#include "Meshing/LODMesher.h"      // For multi-LOD builds

// Forward declarations
class VulkanContext;
class ResourceManager;
class Renderer;

// If ring-buffer chunk destruction is used:
struct QueuedChunkDestruction
{
    VkBuffer       vb = VK_NULL_HANDLE;
    VkDeviceMemory vbMem = VK_NULL_HANDLE;
    VkBuffer       ib = VK_NULL_HANDLE;
    VkDeviceMemory ibMem = VK_NULL_HANDLE;
};

// -----------------------------------------------------------------------------
// Single-lod struct for compatibility with your old code
// -----------------------------------------------------------------------------
struct MeshBuildResult
{
    class Chunk* chunkPtr = nullptr;
    std::vector<Vertex>   verts;
    std::vector<uint32_t> inds;
    int cx = 0, cy = 0, cz = 0;
};

// -----------------------------------------------------------------------------
// VoxelWorld
// -----------------------------------------------------------------------------
class VoxelWorld
{
public:
    enum class MesherType { GREEDY, NAIVE };

    VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr);
    ~VoxelWorld();

    // Optionally attach a Renderer for ring-buffer destruction or any GPU ops
    void setRenderer(Renderer* renderer);

    // Basic world init
    void initWorld();

    // Called each frame to load/unload and handle (re)meshing
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    // Access the chunk manager
    ChunkManager& getChunkManager() { return m_chunkManager; }

    // For debugging or performance stats
    static double getAvgMeshTime();

    // Choose which mesher to use
    void setMesherType(MesherType type) { m_currentMesherType = type; }
    MesherType getMesherType() const { return m_currentMesherType; }

    // Toggle multi-lod usage
    void setUseMultiLOD(bool use) { m_useMultiLOD = use; }
    bool isUsingMultiLOD() const { return m_useMultiLOD; }

private:
    // Helper methods
    void loadOneChunk(const ChunkCoord& c);
    void unloadOneChunk(const ChunkCoord& c);

    void scheduleMeshingForDirtyChunks();
    void pollMeshBuildResults();

    // For single-lod uploads
    void uploadMeshToChunkSingleLOD(class Chunk& chunk,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds);
    void destroyChunkBuffersSingleLOD(class Chunk& chunk);

    // For multi-lod finalization
    void finalizeMultiLOD(MultiLODResult& lodResult);

    // Unused stubs (kept for reference)
    void createBuffer(VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags,
        VkBuffer&, VkDeviceMemory&);
    void copyBuffer(VkBuffer, VkBuffer, VkDeviceSize);
    uint32_t findMemoryType(uint32_t, VkMemoryPropertyFlags);

private:
    // Basic references
    VulkanContext* m_context = nullptr;
    ResourceManager* m_resourceManager = nullptr;
    Renderer* m_renderer = nullptr; // optional

    // Manages all active chunks
    ChunkManager     m_chunkManager;
    // Generates chunk terrain data
    TerrainGenerator m_terrainGenerator;

    // Mesher selection
    GreedyMesher     m_greedyMesher;
    NaiveMesher      m_naiveMesher;
    MesherType       m_currentMesherType = MesherType::GREEDY;

    // If false => single-lod approach (old).
    // If true => multi-lod approach.
    bool m_useMultiLOD = true;

    // Chunk streaming distance
    static constexpr int VIEW_DISTANCE = 4;

    // Queues to handle loading/unloading
    std::deque<ChunkCoord> m_chunksToLoad;
    std::deque<ChunkCoord> m_chunksToUnload;

    // Single-lod pending build results (unused in new approach)
    std::mutex                 m_singleLodMutex;
    std::vector<MeshBuildResult> m_pendingMeshResultsSingleLOD;

    // For multi-lod approach
    std::mutex                 m_multiLODMutex;
    std::vector<MultiLODResult> m_pendingMultiLODResults;
};
