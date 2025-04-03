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

/**
 * For single-lod usage (legacy).
 * We mostly do multi-LOD now, but kept for reference.
 */
struct MeshBuildResult
{
    class Chunk* chunkPtr = nullptr;
    std::vector<Vertex>   verts;
    std::vector<uint32_t> inds;
    int cx = 0, cy = 0, cz = 0;
};

/**
 * The VoxelWorld class manages chunk loading, unloading, terrain generation,
 * and scheduling of meshing tasks (either single-lod or multi-lod).
 */
class VoxelWorld
{
public:
    enum class MesherType { GREEDY, NAIVE };

    VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr);
    ~VoxelWorld();

    /**
     * Optionally attach a Renderer pointer so we can enqueue ring-buffer chunk
     * destruction or do GPU uploads in finalizeMultiLOD.
     */
    void setRenderer(Renderer* renderer);

    /**
     * Initialize the world by generating chunks around the origin.
     */
    void initWorld();

    /**
     * Called each frame to load/unload chunks around the camera,
     * and then schedule meshing tasks for any dirty chunks.
     */
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    /**
     * Returns reference to the chunk manager, which owns all active chunks.
     */
    ChunkManager& getChunkManager() { return m_chunkManager; }

    /**
     * (Optional) Return average meshing time for profiling.
     * Not fully implemented in this snippet.
     */
    static double getAvgMeshTime();

    // -------------------------------------------------------------------------
    // Mesher selection
    // -------------------------------------------------------------------------
    void setMesherType(MesherType type) { m_currentMesherType = type; }
    MesherType getMesherType() const { return m_currentMesherType; }

    // -------------------------------------------------------------------------
    // Multi-LOD toggle
    // -------------------------------------------------------------------------
    void setUseMultiLOD(bool use) { m_useMultiLOD = use; }
    bool isUsingMultiLOD() const { return m_useMultiLOD; }

private:
    /**
     * Creates (and schedules generation for) a chunk at coords c,
     * if it doesn’t already exist.
     */
    void loadOneChunk(const ChunkCoord& c);

    /**
     * Destroys (ring-buffer style) the chunk’s GPU buffers,
     * then removes it from the chunk manager.
     * If the chunk is still isUploading(), we re-queue for next frame.
     */
    void unloadOneChunk(const ChunkCoord& c);

    /**
     * Finds all dirty chunks => schedules a meshing task (multi-LOD or single-lod).
     * For uniform chunks (EMPTY or SOLID) we skip meshing and set isUploading(false).
     */
    void scheduleMeshingForDirtyChunks();

    /**
     * Called each frame to handle results from meshing tasks.
     * For multi-lod, we finalize the new geometry in finalizeMultiLOD().
     */
    void pollMeshBuildResults();

    /**
     * If using single-lod, it would create GPU buffers for chunk->getVerts()...
     * But we now prefer multi-lod finalize in finalizeMultiLOD().
     */
    void uploadMeshToChunkSingleLOD(class Chunk& chunk,
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds);

    /**
     * If single-lod was used, a helper to free chunk’s vertex/index buffers.
     */
    void destroyChunkBuffersSingleLOD(class Chunk& chunk);

    /**
     * For multi-lod approach: after the mesher builds multiple LODs,
     * we create GPU buffers for them, ring-buffer-destroy the old ones,
     * and set isUploading(false).
     */
    void finalizeMultiLOD(MultiLODResult& lodResult);

    // -------------------------------------------------------------------------
    // (Optional) Helpers for single-lod code path (kept for reference).
    // -------------------------------------------------------------------------
    void createBuffer(VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags,
        VkBuffer&, VkDeviceMemory&);
    void copyBuffer(VkBuffer, VkBuffer, VkDeviceSize);
    uint32_t findMemoryType(uint32_t, VkMemoryPropertyFlags);

private:
    // Core references
    VulkanContext* m_context = nullptr;
    ResourceManager* m_resourceManager = nullptr;
    Renderer* m_renderer = nullptr; // optional

    // Main chunk manager
    ChunkManager     m_chunkManager;

    // For generating voxel data in each chunk
    TerrainGenerator m_terrainGenerator;

    // Mesher selection
    GreedyMesher     m_greedyMesher;
    NaiveMesher      m_naiveMesher;
    MesherType       m_currentMesherType = MesherType::GREEDY;

    // If false => single-lod approach
    // If true  => multi-lod approach
    bool m_useMultiLOD = true;

    // We keep the radius to load/unload around the player
    static constexpr int VIEW_DISTANCE = 12;

    // Queues for chunk streaming (not heavily used in this snippet)
    std::deque<ChunkCoord> m_chunksToLoad;
    std::deque<ChunkCoord> m_chunksToUnload;

    // Single-lod pending build results (legacy)
    std::mutex                m_singleLodMutex;
    std::vector<MeshBuildResult> m_pendingMeshResultsSingleLOD;

    // For multi-lod approach, if needed
    std::mutex                 m_multiLODMutex;
    std::vector<MultiLODResult> m_pendingMultiLODResults;
};
