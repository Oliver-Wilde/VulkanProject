#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>
 // [NEW] for m_localMinFilled, m_localMaxFilled
#include <utility>
#include <atomic>  // For std::atomic<size_t>
#include <array>   // For LOD state arrays

#include "IBlockProvider.h"

class Chunk : public IBlockProvider
{
public:
    static const int SIZE_X = 32;
    static const int SIZE_Y = 32;
    static const int SIZE_Z = 32;

    static const int MAX_LOD_LEVELS = 8;

    static std::atomic<size_t> s_totalCPUBytes;

    struct ChunkLOD
    {
        VkBuffer       vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer       indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t       vertexCount = 0;
        uint32_t       indexCount = 0;
    };

    enum class ChunkState
    {
        EMPTY,
        SOLID,
        NORMAL
    };

    Chunk(int worldX, int worldY, int worldZ);
    ~Chunk();

    // ================= IBlockProvider Overrides =================
    virtual int getBlock(int x, int y, int z) const override;
    virtual int getSizeX() const override { return SIZE_X; }
    virtual int getSizeY() const override { return SIZE_Y; }
    virtual int getSizeZ() const override { return SIZE_Z; }
    virtual int baseOffsetX() const override { return m_worldX * SIZE_X; }
    virtual int baseOffsetY() const override { return m_worldY * SIZE_Y; }
    virtual int baseOffsetZ() const override { return m_worldZ * SIZE_Z; }

    void setBlock(int x, int y, int z, int voxelID);

    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

    bool isUploading() const { return m_isUploading; }
    void setIsUploading(bool v) { m_isUploading = v; }

    int worldX() const { return m_worldX; }
    int worldY() const { return m_worldY; }
    int worldZ() const { return m_worldZ; }

    void       setState(ChunkState st) { m_state = st; }
    ChunkState getState()        const { return m_state; }

    uint8_t getUniformBlockID() const { return m_uniformBlockID; }
    void    setUniformBlockID(uint8_t id) { m_uniformBlockID = id; }

    // ---------- Multi-LOD Access ----------
    ChunkLOD& getLODData(int lod) { return m_lods[lod]; }
    const ChunkLOD& getLODData(int lod) const { return m_lods[lod]; }

    bool  isLODGenerated(int lod) const { return m_lodGenerated[lod]; }
    void  setLODGenerated(int lod, bool v) { m_lodGenerated[lod] = v; }

    float getLODErrorValue(int lod) const { return m_lodGeomError[lod]; }
    void  setLODErrorValue(int lod, float e) { m_lodGeomError[lod] = e; }

    // Single-LOD convenience
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

    // Count how many are active vs. empty
    std::pair<size_t, size_t> getVoxelUsage() const;

    // [NEW] Recompute the local min/max of non-air blocks
    // (We call this after generation or any large chunk update.)
    void recalcFilledBounds();

private:
    int  m_worldX;
    int  m_worldY;
    int  m_worldZ;

    bool m_dirty = true;
    bool m_isUploading = false;

    std::vector<uint8_t> m_blocks;
    uint8_t m_uniformBlockID = 0;
    ChunkLOD   m_lods[MAX_LOD_LEVELS];
    ChunkState m_state = ChunkState::NORMAL;

    std::array<bool, MAX_LOD_LEVELS> m_lodGenerated{ false };
    std::array<float, MAX_LOD_LEVELS> m_lodGeomError{ 0.0f };

    // [NEW] For transitioning between states.
    void makeUniform(uint8_t uniformID);
    void makeNormal(uint8_t oldUniformID);

    // [NEW] Tracks whether we have a valid tight bounding box
    bool       m_hasValidBounds = false;
    glm::ivec3 m_localMinFilled{ 0,0,0 };
    glm::ivec3 m_localMaxFilled{ SIZE_X - 1, SIZE_Y - 1, SIZE_Z - 1 };
};
