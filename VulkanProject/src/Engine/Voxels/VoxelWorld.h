// =============================================================================
// VoxelWorld.h   — toggled single‑LOD ⇆ multi‑LOD support
//   • exposes helpers that the .cpp will use to bypass the multi‑LOD path
//   • **no public API changed**: the flag setters already exist
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

#include "ChunkManager.h"
#include "Meshing/NaiveMesher.h"
#include "Meshing/GreedyMesher.h"
#include "Meshing/IMesher.h"
#include "Generation/TerrainGenerator.h"
#include "Meshing/LODMesher.h"

// ── forward declarations ───────────────────────────────────────────────────
class VulkanContext;
class ResourceManager;
class Renderer;

/*─────────────────────────────────────────────────────────────────────────────
  QueuedChunkDestruction  (unchanged, for ring‑buffer frees)
 ────────────────────────────────────────────────────────────────────────────*/
struct QueuedChunkDestruction
{
    VkBuffer       vb = VK_NULL_HANDLE;
    VkDeviceMemory vbMem = VK_NULL_HANDLE;
    VkBuffer       ib = VK_NULL_HANDLE;
    VkDeviceMemory ibMem = VK_NULL_HANDLE;
};

/*─────────────────────────────────────────────────────────────────────────────
  Compatibility struct for single‑LOD path (kept for reference)
 ────────────────────────────────────────────────────────────────────────────*/
struct MeshBuildResult
{
    Chunk* chunkPtr = nullptr;
    std::vector<Vertex>  verts;
    std::vector<uint32_t> inds;
    int cx = 0, cy = 0, cz = 0;
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

    // ── setup / per‑frame update ───────────────────────────────────────────
    void setRenderer(Renderer* renderer);
    void initWorld();
    void updateChunksAroundPlayer(float playerPosX, float playerPosZ);

    // ── public accessors / toggles ─────────────────────────────────────────
    ChunkManager& getChunkManager() { return m_chunkManager; }
    static double getAvgMeshTime();

    void setMesherType(MesherType t) { m_currentMesherType = t; }
    MesherType getMesherType() const { return m_currentMesherType; }

    void setUseMultiLOD(bool use) { m_useMultiLOD = use; }
    bool isUsingMultiLOD() const { return m_useMultiLOD; }

    // ── NEW: upload budget control ─────────────────────────────────────────
    /** Set per‑frame GPU upload budget (bytes + chunk count). */
    void setUploadBudget(size_t bytes, int chunks = 5) { m_uploadBudgetBytes = bytes; m_uploadBudgetChunks = chunks; }
    size_t getUploadBudgetBytes() const { return m_uploadBudgetBytes; }
    int    getUploadBudgetChunks() const { return m_uploadBudgetChunks; }

    /** Current length of the pending‑GPU queue (for ImGui stats). */
    size_t getPendingUploadCount() const { return m_uploadQueue.size(); }

private:
    /*──────── internal helpers ───────────────────────────────────────────*/
    void loadOneChunk(const ChunkCoord& c);
    void unloadOneChunk(const ChunkCoord& c);

    // Central meshing dispatcher; branches on m_useMultiLOD
    void scheduleMeshingForDirtyChunks();

    // Results gatherer: calls the correct specialised path
    void gatherMesherResults();

    // Drain GPU‑upload token bucket (multi‑LOD only)
    void drainUploadQueue();

    // ── single‑LOD‑specific helpers ──────────────────────────────────────
    void scheduleMeshingSingleLOD(Chunk& chunk, const IMesher* baseMesher);
    void gatherSingleLODResults();
    void finalizeSingleLODMesh(MeshBuildResult& result);

    // ── legacy helpers retained for LOD‑0 path ───────────────────────────
    void uploadMeshToChunkSingleLOD(Chunk&, const std::vector<Vertex>&, const std::vector<uint32_t>&);
    void destroyChunkBuffersSingleLOD(Chunk&);

    // ── multi‑LOD helpers (existing) ─────────────────────────────────────
    void finalizeMultiLOD(MultiLODResult&);

    // misc VK helpers (left unchanged)
    void createBuffer(VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags, VkBuffer&, VkDeviceMemory&);
    void copyBuffer(VkBuffer, VkBuffer, VkDeviceSize);
    uint32_t findMemoryType(uint32_t, VkMemoryPropertyFlags);

    /*──────── new mesh‑cache structures ──────────────────────────────────*/
    struct CachedMesh
    {
        // CPU‑side geometry (multi‑LOD)
        std::array<std::vector<Vertex>, MultiLODResult::MAX_LODS> verts;
        std::array<std::vector<uint32_t>, MultiLODResult::MAX_LODS> inds;
    };

    struct PendingUpload
    {
        MultiLODResult mlr;   // ready CPU geometry
        size_t         bytes; // total VB+IB size
        uint64_t       hash;  // content hash for cache insertion
    };

private:
    // ── core refs ─────────────────────────────────────────────────────────
    VulkanContext* m_context = nullptr;
    ResourceManager* m_resourceManager = nullptr;
    Renderer* m_renderer = nullptr;

    // ── chunk data & generation ───────────────────────────────────────────
    ChunkManager     m_chunkManager;
    TerrainGenerator m_terrainGenerator;

    GreedyMesher     m_greedyMesher;
    NaiveMesher      m_naiveMesher;
    MesherType       m_currentMesherType = MesherType::GREEDY;
    bool             m_useMultiLOD = false;   // runtime toggle

    // ── streaming distance ───────────────────────────────────────────────
    static constexpr int VIEW_DISTANCE = 16;

    std::deque<ChunkCoord> m_chunksToLoad;
    std::deque<ChunkCoord> m_chunksToUnload;

    /*──────── legacy single‑LOD buffers ──────────────────────────────────*/
    std::mutex                   m_singleLodMutex;
    std::vector<MeshBuildResult> m_pendingMeshResultsSingleLOD;

    /*──────── async multi‑LOD queues ────────────────────────────────────*/
    std::mutex                  m_multiLODMutex;
    std::vector<MultiLODResult> m_pendingMultiLODResults; // freshly meshed

    /*──────── token‑bucket upload queue (multi‑LOD only) ───────────────*/
    std::deque<PendingUpload>   m_uploadQueue;
    size_t                      m_uploadBudgetBytes = 2 * 1024 * 1024; // 2 MiB per frame
    int                         m_uploadBudgetChunks = 5;               // 5 chunks per frame

    /*──────── RAM mesh cache with simple LRU eviction ───────────────────*/
    std::unordered_map<uint64_t, CachedMesh> m_meshCache;   // hash → mesh
    std::list<uint64_t>                      m_cacheLRU;    // most‑recent front
    static constexpr size_t                  MAX_CACHE_SIZE = 256;
};
