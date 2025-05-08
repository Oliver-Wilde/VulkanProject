// =============================================================================
// VoxelWorld.h   — benchmark-ready (2025-04-30)
//   • Removes duplicate getters / type clashes
//   • Adds per-chunk timing + Morton ID fields
//   • Keeps public API identical for non-BENCHMARK_MODE builds
// =============================================================================
#pragma once

#include <vulkan/vulkan.h>

#include <vector>
#include <deque>
#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <list>
#include <chrono>
#include <algorithm>

#include "ChunkManager.h"
#include "Meshing/NaiveMesher.h"
#include "Meshing/GreedyMesher.h"
#include "Meshing/IMesher.h"
#include "Generation/TerrainGenerator.h"
#include "Meshing/LODMesher.h"

#ifdef BENCHMARK_MODE
#include "Engine/Utils/BenchmarkLogger.h"
#endif

// ── forward declarations ───────────────────────────────────────────────────
class VulkanContext;
class ResourceManager;
class Renderer;

/*─────────────────────────────────────────────────────────────────────────────
  QueuedChunkDestruction  (unchanged)
 ────────────────────────────────────────────────────────────────────────────*/
struct QueuedChunkDestruction
{
    VkBuffer       vb = VK_NULL_HANDLE;
    VkDeviceMemory vbMem = VK_NULL_HANDLE;
    VkBuffer       ib = VK_NULL_HANDLE;
    VkDeviceMemory ibMem = VK_NULL_HANDLE;
};

/*─────────────────────────────────────────────────────────────────────────────
  MeshBuildResult – single-LOD worker output
 ────────────────────────────────────────────────────────────────────────────*/
struct MeshBuildResult
{
    Chunk* chunkPtr = nullptr;
    std::vector<Vertex>  verts;
    std::vector<uint32_t> inds;
    int                  cx = 0, cy = 0, cz = 0;

#ifdef BENCHMARK_MODE
    uint32_t morton = 0;   ///< 3×10-bit Morton code for the chunk
    uint32_t meshingUs = 0;   ///< wall-clock micro-seconds spent in generateMesh()
#endif
};

/*=============================================================================
  VoxelWorld
=============================================================================*/
class VoxelWorld
{
public:
    enum class MesherType { GREEDY, NAIVE };

    VoxelWorld(VulkanContext* context, ResourceManager* resourceMgr);
    ~VoxelWorld();

    // ── setup / per-frame update ─────────────────────────────────────────
    void setRenderer(Renderer* renderer);
    void initWorld();
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    // ── public accessors / toggles ──────────────────────────────────────
    ChunkManager& getChunkManager() { return m_chunkManager; }
    static double getAvgMeshTime();

    void setMesherType(MesherType t) { m_currentMesherType = t; }
    MesherType getMesherType() const { return m_currentMesherType; }

    void setUseMultiLOD(bool use) { m_useMultiLOD = use; }
    bool isUsingMultiLOD() const { return m_useMultiLOD; }

    void forceRebuildAllChunks();   // mark all dirty

    // ── vertical streaming range ────────────────────────────────────────
    inline void setVerticalRange(int v) { m_verticalRange = std::clamp(v, 0, 4); }
    inline int  getVerticalRange() const { return m_verticalRange; }

    // ── upload-budget control ───────────────────────────────────────────
    void   setUploadBudget(size_t bytes, int chunks = 5);
    size_t getUploadBudgetBytes() const { return m_uploadBudgetBytes; }
    int    getUploadBudgetChunks() const { return m_uploadBudgetChunks; }
    size_t getPendingUploadCount() const { return m_uploadQueue.size(); }

    static constexpr size_t MIN_UPLOAD_BUDGET_BYTES = 512 * 1024;        // 0.5 MiB
    static constexpr size_t MAX_UPLOAD_BUDGET_BYTES = 16 * 1024 * 1024;  // 16 MiB
    static constexpr int    MIN_UPLOAD_BUDGET_CHUNKS = 1;
    static constexpr int    MAX_UPLOAD_BUDGET_CHUNKS = 32;

#ifdef BENCHMARK_MODE
    /* Per-frame telemetry consumed by BenchmarkLogger */
    float     getCpuMeshingMsLastFrame()  const { return m_cpuMeshingMsLastFrame; }
    uint32_t  getChunksRebuiltLastFrame() const { return m_chunksRebuiltLastFrame; }
#endif

private:
    /*──────── world-management helpers (unchanged declarations) ──────────*/
    void loadOneChunk(const ChunkCoord&);
    void unloadOneChunk(const ChunkCoord&);

    void scheduleMeshingForDirtyChunks();
    void gatherMesherResults();
    void drainUploadQueue();

    // single-LOD
    void scheduleMeshingSingleLOD(Chunk&, const IMesher*);
    void gatherSingleLODResults();
    void finalizeSingleLODMesh(MeshBuildResult&);
    void uploadMeshToChunkSingleLOD(Chunk&,
        const std::vector<Vertex>&, const std::vector<uint32_t>&);
    void destroyChunkBuffersSingleLOD(Chunk&);

    // multi-LOD
    void finalizeMultiLOD(MultiLODResult&);

    // misc VK helpers
    void createBuffer(VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags,
        VkBuffer&, VkDeviceMemory&);
    void copyBuffer(VkBuffer, VkBuffer, VkDeviceSize);
    uint32_t findMemoryType(uint32_t, VkMemoryPropertyFlags);

    /*──────── mesh cache & upload queue (unchanged declarations) ─────────*/
    struct CachedMesh
    {
        std::array<std::vector<Vertex>, MultiLODResult::MAX_LODS> verts;
        std::array<std::vector<uint32_t>, MultiLODResult::MAX_LODS> inds;
    };
    struct PendingUpload
    {
        MultiLODResult mlr;
        size_t         bytes;
        uint64_t       hash;
    };

    // ── core refs ───────────────────────────────────────────────────────
    VulkanContext* m_context = nullptr;
    ResourceManager* m_resourceManager = nullptr;
    Renderer* m_renderer = nullptr;

    // ── chunk data & generation ─────────────────────────────────────────
    ChunkManager     m_chunkManager;
    TerrainGenerator m_terrainGenerator;

    GreedyMesher     m_greedyMesher;
    NaiveMesher      m_naiveMesher;
    MesherType       m_currentMesherType = MesherType::GREEDY;
    bool             m_useMultiLOD = false;

    // streaming distance
    static constexpr int VIEW_DISTANCE = 16;
    std::deque<ChunkCoord> m_chunksToLoad;
    std::deque<ChunkCoord> m_chunksToUnload;

    /*──────── single-LOD worker queues ─────────────────────────────────*/
    std::mutex                   m_singleLodMutex;
    std::vector<MeshBuildResult> m_pendingMeshResultsSingleLOD;

    /*──────── multi-LOD worker queues ─────────────────────────────────*/
    std::mutex                  m_multiLODMutex;
    std::vector<MultiLODResult> m_pendingMultiLODResults;

    /*──────── token-bucket upload queue ───────────────────────────────*/
    std::deque<PendingUpload>   m_uploadQueue;
    size_t m_uploadBudgetBytes = 2 * 1024 * 1024; // 2 MiB / frame
    int    m_uploadBudgetChunks = 5;                // 5 chunks / frame

    // mesh-cache
    std::unordered_map<uint64_t, CachedMesh> m_meshCache;
    std::list<uint64_t>                      m_cacheLRU;
    static constexpr size_t                  MAX_CACHE_SIZE = 256;

    // adaptive frame-time tracker (unchanged)
    static constexpr size_t FRAME_TIME_BUFFER = 120;
    std::deque<float>                        m_frameTimeMs;
    std::chrono::high_resolution_clock::time_point m_lastFrameTS{};
    int                                      m_adjustCooldownFrames = 0;

    // vertical range
    int m_verticalRange = 1;   // ±1 Y-layer

#ifdef BENCHMARK_MODE
    /*──────── per-frame counters for CSV logging ──────────────────────*/
    float    m_cpuMeshingMsLastFrame = 0.0f;
    uint32_t m_chunksRebuiltLastFrame = 0;
#endif
};


