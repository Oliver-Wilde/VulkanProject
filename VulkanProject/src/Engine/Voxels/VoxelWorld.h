// =============================================================================
// VoxelWorld.h   — adaptive upload-budget + frame-time tracker
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
#include <chrono>          // frame-time stamps
#include <algorithm>       // std::clamp

#include "ChunkManager.h"
#include "Meshing/NaiveMesher.h"
#include "Meshing/GreedyMesher.h"
#include "Meshing/IMesher.h"
#include "Generation/TerrainGenerator.h"
#include "Meshing/LODMesher.h"

#ifdef BENCHMARK_MODE
#include "Engine/Utils/BenchmarkLogger.h"   // only for inline accessors; no cpp-time dep
#endif

// ── forward declarations ───────────────────────────────────────────────────
class VulkanContext;
class ResourceManager;
class Renderer;

/*─────────────────────────────────────────────────────────────────────────────
  QueuedChunkDestruction  (unchanged, for ring-buffer frees)
 ────────────────────────────────────────────────────────────────────────────*/
struct QueuedChunkDestruction
{
    VkBuffer       vb = VK_NULL_HANDLE;
    VkDeviceMemory vbMem = VK_NULL_HANDLE;
    VkBuffer       ib = VK_NULL_HANDLE;
    VkDeviceMemory ibMem = VK_NULL_HANDLE;
};

/*─────────────────────────────────────────────────────────────────────────────
  Compatibility struct for single-LOD path (kept for reference)
 ────────────────────────────────────────────────────────────────────────────*/
struct MeshBuildResult
{
    Chunk* chunkPtr = nullptr;
    std::vector<Vertex>   verts;
    std::vector<uint32_t> inds;
    int cx = 0, cy = 0, cz = 0;

#ifdef BENCHMARK_MODE
    float      cpuMs = 0.0f;      // ← NEW
    uint64_t   morton = 0;         // ← NEW (pre-computed ID)
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

    // ── setup / per-frame update ──────────────────────────────────────────
    void setRenderer(Renderer* renderer);
    void initWorld();
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    // ── public accessors / toggles ────────────────────────────────────────
    ChunkManager& getChunkManager() { return m_chunkManager; }
    static double getAvgMeshTime();

    void setMesherType(MesherType t) { m_currentMesherType = t; }
    MesherType getMesherType() const { return m_currentMesherType; }

    void setUseMultiLOD(bool use) { m_useMultiLOD = use; }
    bool isUsingMultiLOD() const { return m_useMultiLOD; }

    /** Marks every loaded chunk dirty so its geometry is rebuilt next frame. */
    void forceRebuildAllChunks();

    // ── runtime control over vertical streaming range ───────────────────
    inline void setVerticalRange(int v) { m_verticalRange = std::clamp(v, 0, 4); }
    inline int  getVerticalRange() const { return m_verticalRange; }

    // ── upload budget control ─────────────────────────────────────────────
    void setUploadBudget(size_t bytes, int chunks = 5);
    size_t getUploadBudgetBytes()  const { return m_uploadBudgetBytes; }
    int    getUploadBudgetChunks() const { return m_uploadBudgetChunks; }
    size_t getPendingUploadCount() const { return m_uploadQueue.size(); }

    inline float    getCpuMeshingMsLastFrame()   const { return m_cpuMeshingMsLastFrame; }
    inline uint32_t getChunksRebuiltLastFrame()  const { return m_chunksRebuiltLastFrame; }
    inline uint32_t getUploadBudgetBytes()       const { return m_uploadBudgetBytes; }

#ifdef BENCHMARK_MODE
    /* ─────────── frame-level telemetry for BenchmarkLogger ─────────── */
    float     getMeshingCpuMsLastFrame()  const { return m_cpuMeshingMsLastFrame; }
    uint32_t  getChunksRebuiltLastFrame() const { return m_chunksRebuiltLastFrame; }
#endif

private:
    /*──────── internal helpers ───────────────────────────────────────────*/
    void loadOneChunk(const ChunkCoord& c);
    void unloadOneChunk(const ChunkCoord& c);

    void scheduleMeshingForDirtyChunks();   // central dispatcher
    void gatherMesherResults();             // collects finished jobs
    void drainUploadQueue();                // token-bucket drain (multi-LOD)

    // ── single-LOD helpers ──────────────────────────────────────────────
    void scheduleMeshingSingleLOD(Chunk&, const IMesher*);
    void gatherSingleLODResults();
    void finalizeSingleLODMesh(MeshBuildResult&);

    void uploadMeshToChunkSingleLOD(Chunk&,
        const std::vector<Vertex>&, const std::vector<uint32_t>&);
    void destroyChunkBuffersSingleLOD(Chunk&);

    // ── multi-LOD helper (already present) ───────────────────────────────
    void finalizeMultiLOD(MultiLODResult&);

    // misc VK helpers
    void createBuffer(VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags,
        VkBuffer&, VkDeviceMemory&);
    void copyBuffer(VkBuffer, VkBuffer, VkDeviceSize);
    uint32_t findMemoryType(uint32_t, VkMemoryPropertyFlags);

    /*──────── new mesh-cache structures ──────────────────────────────────*/
    struct CachedMesh
    {
        std::array<std::vector<Vertex>, MultiLODResult::MAX_LODS>    verts;
        std::array<std::vector<uint32_t>, MultiLODResult::MAX_LODS>  inds;
    };

    struct PendingUpload
    {
        MultiLODResult mlr;     // ready CPU geometry
        size_t         bytes;   // total VB+IB size
        uint64_t       hash; 
        // content hash for cache insertion
    };

private:
    // ── core refs ─────────────────────────────────────────────────────────
    VulkanContext* m_context = nullptr;
    ResourceManager* m_resourceManager = nullptr;
    Renderer* m_renderer = nullptr;

    // ── chunk data & generation ───────────────────────────────────────────
    ChunkManager      m_chunkManager;
    TerrainGenerator  m_terrainGenerator;

    GreedyMesher      m_greedyMesher;
    NaiveMesher       m_naiveMesher;
    MesherType        m_currentMesherType = MesherType::GREEDY;
    bool              m_useMultiLOD = false;

    // ── streaming distance ───────────────────────────────────────────────
    static constexpr int VIEW_DISTANCE = 16;

    std::deque<ChunkCoord> m_chunksToLoad;
    std::deque<ChunkCoord> m_chunksToUnload;

    /*──────── legacy single-LOD buffers ──────────────────────────────────*/
    std::mutex                   m_singleLodMutex;
    std::vector<MeshBuildResult> m_pendingMeshResultsSingleLOD;

    /*──────── async multi-LOD queues ────────────────────────────────────*/
    std::mutex                  m_multiLODMutex;
    std::vector<MultiLODResult> m_pendingMultiLODResults;

    /*──────── token-bucket upload queue (multi-LOD only) ───────────────*/
    std::deque<PendingUpload>   m_uploadQueue;

    // adaptive upload budget limits
    static constexpr size_t MIN_UPLOAD_BUDGET_BYTES = 512 * 1024;        // 0.5 MiB
    static constexpr size_t MAX_UPLOAD_BUDGET_BYTES = 16 * 1024 * 1024;  // 16 MiB
    static constexpr int    MIN_UPLOAD_BUDGET_CHUNKS = 1;
    static constexpr int    MAX_UPLOAD_BUDGET_CHUNKS = 32;

    size_t m_uploadBudgetBytes = 2 * 1024 * 1024;   // starts at 2 MiB / frame
    int    m_uploadBudgetChunks = 5;                 // 5 chunks / frame

    /*──────── RAM mesh cache with simple LRU eviction ───────────────────*/
    std::unordered_map<uint64_t, CachedMesh> m_meshCache;   // hash → mesh
    std::list<uint64_t>                      m_cacheLRU;    // MRU front
    static constexpr size_t                  MAX_CACHE_SIZE = 256;

    /*──────── rolling frame-time tracker ────────────────────────────────*/
    static constexpr size_t FRAME_TIME_BUFFER = 120;
    std::deque<float>                        m_frameTimeMs;
    std::chrono::high_resolution_clock::time_point m_lastFrameTS{};
    int                                      m_adjustCooldownFrames = 0;

    /*──────── vertical chunk-column range (runtime tweakable) ───────────*/
    int m_verticalRange = 1;   // ±1 Y-layer

#ifdef BENCHMARK_MODE
    /*──────── per-frame counters for CSV logging ───────────────────────*/
    float    m_cpuMeshingMsLastFrame = 0.0f;
    uint32_t m_chunksRebuiltLastFrame = 0;
#endif
};
