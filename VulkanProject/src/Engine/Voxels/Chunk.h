#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>
#include <utility>

// Single chunk with optional multi-LOD
class Chunk
{
public:
    // Standard size
    static const int SIZE_X = 16;
    static const int SIZE_Y = 16;
    static const int SIZE_Z = 16;

    // We'll support up to 3 LOD levels
    static const int MAX_LOD_LEVELS = 8;

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

    Chunk(int worldX, int worldY, int worldZ);
    ~Chunk();

    // 3D block access
    int  getBlock(int x, int y, int z) const;
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

    // ----------- MULTI-LOD Access -----------
    ChunkLOD& getLODData(int lod) { return m_lods[lod]; }
    const ChunkLOD& getLODData(int lod) const { return m_lods[lod]; }

    // ---------- SINGLE-LOD Compatibility -----------
    // Old calls: chunk->getVertexBuffer(), etc. => they read/write LOD0
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

    // All block data
    std::vector<int> m_blocks;

    // MULTI-LOD: Up to 3 LODs
    ChunkLOD m_lods[MAX_LOD_LEVELS];
};
