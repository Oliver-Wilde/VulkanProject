#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>
#include <utility>
#include <atomic> // <-- For std::atomic<size_t>
#include <array>  // For additional LOD state tracking arrays

// Single chunk with optional multi-LOD
class Chunk
{
public:
    // Standard size
    static const int SIZE_X = 16;
    static const int SIZE_Y = 128;
    static const int SIZE_Z = 16;

    // We'll support up to 3 LOD levels (but code currently uses up to 8)
    static const int MAX_LOD_LEVELS = 8;

    /**
     * Tracks the total CPU bytes used by all chunk voxel arrays across the entire game.
     * Each chunk constructor increments this, destructor decrements.
     */
    static std::atomic<size_t> s_totalCPUBytes;

    // Each LOD has its own GPU data
    struct ChunkLOD
    {
        VkBuffer       vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer       indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t       vertexCount = 0;
        uint32_t       indexCount = 0;
    };

    /**
     * Represents whether the chunk is fully EMPTY (all voxel=0),
     * fully SOLID (all voxel same & !=0),
     * or NORMAL (mixed).
     */
    enum class ChunkState
    {
        EMPTY,
        SOLID,
        NORMAL
    };

    Chunk(int worldX, int worldY, int worldZ);
    ~Chunk();

    // 3D block access
    // [Note] We'll still return int from getBlock() / setBlock()
    // even though we store in a uint8_t array internally.
    virtual int getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, int voxelID);

    // Dirty & uploading flags
    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

    bool isUploading() const { return m_isUploading; }
    void setIsUploading(bool value) { m_isUploading = value; }

    // World coords
    int worldX() const { return m_worldX; }
    int worldY() const { return m_worldY; }
    int worldZ() const { return m_worldZ; }

    // Chunk state (EMPTY, SOLID, NORMAL)
    void setState(ChunkState st) { m_state = st; }
    ChunkState getState() const { return m_state; }

    // ----------- MULTI-LOD Access -----------
    ChunkLOD& getLODData(int lod) { return m_lods[lod]; }
    const ChunkLOD& getLODData(int lod) const { return m_lods[lod]; }

    // Provide additional control over LOD readiness/quality
    // (added to support more sophisticated logic for transitions / error metrics)
    bool isLODGenerated(int lod) const { return m_lodGenerated[lod]; }
    void setLODGenerated(int lod, bool ready) { m_lodGenerated[lod] = ready; }

    float getLODErrorValue(int lod) const { return m_lodGeomError[lod]; }
    void  setLODErrorValue(int lod, float e) { m_lodGeomError[lod] = e; }

    // ---------- SINGLE-LOD Compatibility -----------
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

    // AABB for culling
    void getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const;

    // Count how many are air vs. solid
    std::pair<size_t, size_t> getVoxelUsage() const;

private:
    int m_worldX;
    int m_worldY;
    int m_worldZ;

    bool m_dirty = true;
    bool m_isUploading = false;

    // [CHANGED] Store each voxel in a uint8_t instead of int
    // This reduces memory from 4 bytes/voxel to 1 byte/voxel
    std::vector<uint8_t> m_blocks; // [CHANGED]

    // MULTI-LOD: Up to 8 LOD levels
    ChunkLOD m_lods[MAX_LOD_LEVELS];

    // Tracks if the chunk is empty, solid, or normal
    ChunkState m_state = ChunkState::NORMAL;

    // Tracks whether each LOD has been generated yet
    std::array<bool, MAX_LOD_LEVELS>  m_lodGenerated{ false };

    // Optional: store some geometric error metric for each LOD
    std::array<float, MAX_LOD_LEVELS> m_lodGeomError{ 0.0f };
};
