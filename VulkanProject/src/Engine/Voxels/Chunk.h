#pragma once

#include <vector>
#include <array>
#include <utility>
#include <atomic>
#include <cstdint>               // NEW: uint64_t
#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>

#include "IBlockProvider.h"

/*─────────────────────────────────────────────────────────────────────────────
  Chunk — data container for a 32³‑voxel region plus multi‑LOD GPU handles.
  This version adds a 64‑bit “content hash” so we can detect identical chunks
  for RAM‑mesh caching, and plumbing to mark that hash dirty on edits.
 ────────────────────────────────────────────────────────────────────────────*/
class Chunk : public IBlockProvider
{
public:
    // ── compile‑time constants ────────────────────────────────────────────
    static const int SIZE_X = 32;
    static const int SIZE_Y = 32;
    static const int SIZE_Z = 32;

    static const int MAX_LOD_LEVELS = 8;

    /** Global counter tracking host‑side voxel array bytes. */
    static std::atomic<size_t> s_totalCPUBytes;

    // ── GPU geometry container ────────────────────────────────────────────
    struct ChunkLOD
    {
        VkBuffer       vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer       indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t       vertexCount = 0;
        uint32_t       indexCount = 0;
    };

    enum class ChunkState { EMPTY, SOLID, NORMAL };

    Chunk(int worldX, int worldY, int worldZ);
    ~Chunk();

    // ── IBlockProvider overrides ──────────────────────────────────────────
    int  getBlock(int x, int y, int z) const override;
    int  getSizeX() const override { return SIZE_X; }
    int  getSizeY() const override { return SIZE_Y; }
    int  getSizeZ() const override { return SIZE_Z; }
    int  baseOffsetX() const override { return m_worldX * SIZE_X; }
    int  baseOffsetY() const override { return m_worldY * SIZE_Y; }
    int  baseOffsetZ() const override { return m_worldZ * SIZE_Z; }

    // ── voxel mutation ────────────────────────────────────────────────────
    void setBlock(int x, int y, int z, int voxelID);

    // ── dirty / uploading flags ───────────────────────────────────────────
    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

    bool isUploading() const { return m_isUploading; }
    void setIsUploading(bool v) { m_isUploading = v; }

    // ── world‑coords helpers ──────────────────────────────────────────────
    int worldX() const { return m_worldX; }
    int worldY() const { return m_worldY; }
    int worldZ() const { return m_worldZ; }

    // ── state helpers ─────────────────────────────────────────────────────
    void       setState(ChunkState st) { m_state = st; }
    ChunkState getState() const { return m_state; }

    uint8_t getUniformBlockID() const { return m_uniformBlockID; }
    void    setUniformBlockID(uint8_t id) { m_uniformBlockID = id; }

    // ── LOD accessors ─────────────────────────────────────────────────────
    ChunkLOD& getLODData(int lod) { return m_lods[lod]; }
    const ChunkLOD& getLODData(int lod) const { return m_lods[lod]; }

    bool  isLODGenerated(int lod) const { return m_lodGenerated[lod]; }
    void  setLODGenerated(int lod, bool v) { m_lodGenerated[lod] = v; }

    float getLODErrorValue(int lod) const { return m_lodGeomError[lod]; }
    void  setLODErrorValue(int lod, float e) { m_lodGeomError[lod] = e; }

    // Single‑LOD convenience (LOD 0 == full‑res geometry)
    VkBuffer       getVertexBuffer()   const { return m_lods[0].vertexBuffer; }
    VkDeviceMemory getVertexMemory()   const { return m_lods[0].vertexMemory; }
    VkBuffer       getIndexBuffer()    const { return m_lods[0].indexBuffer; }
    VkDeviceMemory getIndexMemory()    const { return m_lods[0].indexMemory; }
    uint32_t       getVertexCount()    const { return m_lods[0].vertexCount; }
    uint32_t       getIndexCount()     const { return m_lods[0].indexCount; }

    void setVertexBuffer(VkBuffer vb) { m_lods[0].vertexBuffer = vb; }
    void setVertexMemory(VkDeviceMemory vm) { m_lods[0].vertexMemory = vm; }
    void setIndexBuffer(VkBuffer ib) { m_lods[0].indexBuffer = ib; }
    void setIndexMemory(VkDeviceMemory im) { m_lods[0].indexMemory = im; }
    void setVertexCount(uint32_t vc) { m_lods[0].vertexCount = vc; }
    void setIndexCount(uint32_t ic) { m_lods[0].indexCount = ic; }

    // ── culling‑related helpers ───────────────────────────────────────────
    void getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const;
    std::pair<size_t, size_t> getVoxelUsage() const;
    void recalcFilledBounds();   // recompute tight bounds of non‑air voxels

    // ── NEW: content‑hash helpers ─────────────────────────────────────────
    /** Returns a stable 64‑bit hash of voxel contents/state.
        Expensive to compute once; cached until markHashDirty() is called. */
    uint64_t getContentHash();

    /** Call when voxel data mutates to invalidate the cached hash. */
    void markHashDirty() { m_hashDirty = true; }

private:
    // ── data members ──────────────────────────────────────────────────────
    int  m_worldX, m_worldY, m_worldZ;

    bool m_dirty = true;
    bool m_isUploading = false;

    std::vector<uint8_t> m_blocks;           // SIZE_X*SIZE_Y*SIZE_Z when NORMAL
    uint8_t              m_uniformBlockID = 0;

    ChunkLOD   m_lods[MAX_LOD_LEVELS];
    ChunkState m_state = ChunkState::NORMAL;

    std::array<bool, MAX_LOD_LEVELS> m_lodGenerated{ false };
    std::array<float, MAX_LOD_LEVELS> m_lodGeomError{ 0.0f };

    // ── state‑transition helpers ──────────────────────────────────────────
    void makeUniform(uint8_t uniformID);
    void makeNormal(uint8_t oldUniformID);

    // ── tight‑bounds cache for frustum culling ────────────────────────────
    bool       m_hasValidBounds = false;
    glm::ivec3 m_localMinFilled{ 0, 0, 0 };
    glm::ivec3 m_localMaxFilled{ SIZE_X - 1, SIZE_Y - 1, SIZE_Z - 1 };

    // ── NEW: content‑hash bookkeeping ─────────────────────────────────────
    uint64_t m_cachedHash = 0;
    bool     m_hashDirty = true;
};
