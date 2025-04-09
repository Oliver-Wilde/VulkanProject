#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>
#include <utility>
#include <atomic>  // For std::atomic<size_t>
#include <array>   // For LOD state arrays

// Include the IBlockProvider interface
#include "IBlockProvider.h"

class Chunk : public IBlockProvider
{
public:
    // Size in voxels
    static const int SIZE_X = 32;
    static const int SIZE_Y = 32;
    static const int SIZE_Z = 32;

    // We'll support up to 8 LOD levels
    static const int MAX_LOD_LEVELS = 8;

    /**
     * Tracks the total CPU bytes used by all chunk voxel arrays across the entire game.
     * Each chunk constructor increments this, destructor decrements if using normal storage.
     */
    static std::atomic<size_t> s_totalCPUBytes;

    // GPU buffer info for each LOD
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

    // Constructor & Destructor
    Chunk(int worldX, int worldY, int worldZ);
    ~Chunk();

    // ========== IBlockProvider Overrides ==========
    // (These inform the mesher about chunk size & base offset in the world)
    virtual int getBlock(int x, int y, int z) const override;  // returns block ID or -1 if out-of-bounds
    virtual int getSizeX() const override { return SIZE_X; }
    virtual int getSizeY() const override { return SIZE_Y; }
    virtual int getSizeZ() const override { return SIZE_Z; }

    virtual int baseOffsetX() const override { return m_worldX * SIZE_X; }
    virtual int baseOffsetY() const override { return m_worldY * SIZE_Y; }
    virtual int baseOffsetZ() const override { return m_worldZ * SIZE_Z; }

    // 3D block setter
    void setBlock(int x, int y, int z, int voxelID);

    // Dirty & uploading flags
    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

    bool isUploading() const { return m_isUploading; }
    void setIsUploading(bool v) { m_isUploading = v; }

    // World coords (which chunk in a chunk grid, e.g. chunk(2,0,-3))
    int worldX() const { return m_worldX; }
    int worldY() const { return m_worldY; }
    int worldZ() const { return m_worldZ; }

    // Chunk state (EMPTY, SOLID, NORMAL)
    void       setState(ChunkState st) { m_state = st; }
    ChunkState getState()        const { return m_state; }

    // If chunk is SOLID or EMPTY, we store one uniform block ID
    // If chunk is NORMAL, we have a full voxel array
    uint8_t getUniformBlockID() const { return m_uniformBlockID; }
    void    setUniformBlockID(uint8_t id) { m_uniformBlockID = id; }

    // ----------- MULTI-LOD Access -----------
    ChunkLOD& getLODData(int lod) { return m_lods[lod]; }
    const ChunkLOD& getLODData(int lod) const { return m_lods[lod]; }

    bool  isLODGenerated(int lod) const { return m_lodGenerated[lod]; }
    void  setLODGenerated(int lod, bool v) { m_lodGenerated[lod] = v; }

    float getLODErrorValue(int lod) const { return m_lodGeomError[lod]; }
    void  setLODErrorValue(int lod, float e)
    {
        m_lodGeomError[lod] = e;
    }

    // ---------- SINGLE-LOD Convenience -----------
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
    int  m_worldX;
    int  m_worldY;
    int  m_worldZ;

    bool m_dirty = true;
    bool m_isUploading = false;

    // The actual voxel array, used only if state == NORMAL.
    // If state == EMPTY or SOLID, we free this array and rely on m_uniformBlockID.
    std::vector<uint8_t> m_blocks;

    // The uniform block ID used if state == SOLID or EMPTY.
    // 0 => Air if EMPTY, or some nonzero ID if SOLID.
    uint8_t m_uniformBlockID = 0;

    // Multi-LOD data
    ChunkLOD   m_lods[MAX_LOD_LEVELS];
    ChunkState m_state = ChunkState::NORMAL;

    std::array<bool, MAX_LOD_LEVELS> m_lodGenerated{ false };
    std::array<float, MAX_LOD_LEVELS> m_lodGeomError{ 0.0f };

    // ------------------ NEW PRIVATE METHODS ------------------

    /**
     * Transition this chunk to uniform storage (SOLID or EMPTY).
     * This frees m_blocks and sets m_uniformBlockID to uniformID.
     */
    void makeUniform(uint8_t uniformID);

    /**
     * Transition this chunk to normal storage.
     * This allocates m_blocks if empty, fills with oldUniformID.
     */
    void makeNormal(uint8_t oldUniformID);
};
