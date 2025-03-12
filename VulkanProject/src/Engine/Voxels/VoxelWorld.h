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
    std::vector<Vertex>    verts;
    std::vector<uint32_t>  inds;
    int cx = 0, cy = 0, cz = 0;
};

// -----------------------------------------------------------------------------
// VoxelWorld
// -----------------------------------------------------------------------------
class VoxelWorld
{
public:
    enum class MesherType { GREEDY, NAIVE };

    // Two-arg constructor
    VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr);
    ~VoxelWorld();

    // If you need a Renderer pointer for ring-buffer destruction:
    void setRenderer(Renderer* renderer);

    // World init (generate some initial region)
    void initWorld();
    // Called each frame to load/unload and (re)mesh chunks
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    ChunkManager& getChunkManager() { return m_chunkManager; }

    // For debugging or stats
    static double getAvgMeshTime();

    // Choose mesher (greedy or naive)
    void setMesherType(MesherType type) { m_currentMesherType = type; }
    MesherType getMesherType() const { return m_currentMesherType; }

    // Toggle multi-lod usage
    void setUseMultiLOD(bool use) { m_useMultiLOD = use; }
    bool isUsingMultiLOD() const { return m_useMultiLOD; }

private:
    VulkanContext* m_context = nullptr;
    ResourceManager* m_resourceManager = nullptr;
    Renderer* m_renderer = nullptr; // set later if needed

    ChunkManager     m_chunkManager;
    TerrainGenerator m_terrainGenerator;

    // View distance for loads/unloads
    static constexpr int VIEW_DISTANCE = 16;

    // We keep references to both meshers; choose at runtime
    GreedyMesher  m_greedyMesher;
    NaiveMesher   m_naiveMesher;
    MesherType    m_currentMesherType = MesherType::GREEDY;

    // If false => single-lod approach (old code), 
    // if true => multi-lod approach
    bool m_useMultiLOD = true;

    // Queues for chunk loading/unloading
    std::deque<ChunkCoord> m_chunksToLoad;
    std::deque<ChunkCoord> m_chunksToUnload;

    // --------------- Single-lod ---------------
    // (Old approach) Completed mesh results
    // (You likely had a global static in .cpp; 
    //  we can store it as a member instead.)
    std::mutex              m_singleLodMutex;
    std::vector<MeshBuildResult> m_pendingMeshResultsSingleLOD;

    // --------------- Multi-lod ---------------
    // (New approach) LODMesher::buildAllLODs(...) => MultiLODResult
    // We store those results here.
    std::mutex              m_multiLODMutex;
    std::vector<MultiLODResult> m_pendingMultiLODResults;

    // ----------------- Internal Helpers -----------------
    void loadOneChunk(const ChunkCoord& c);
    void unloadOneChunk(const ChunkCoord& c);

    void scheduleMeshingForDirtyChunks();
    void pollMeshBuildResults(); // dispatch single-lod vs multi-lod

    // Single-chunk GPU upload or destruction
    void uploadMeshToChunkSingleLOD(class Chunk& chunk,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds);
    void destroyChunkBuffersSingleLOD(class Chunk& chunk);

    // multi-lod finalization
    void finalizeMultiLOD(MultiLODResult& lodResult);

    // Not used in your new code (kept for reference)
    void createBuffer(VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags,
        VkBuffer&, VkDeviceMemory&);
    void copyBuffer(VkBuffer, VkBuffer, VkDeviceSize);
    uint32_t findMemoryType(uint32_t, VkMemoryPropertyFlags);
};
