#pragma once

#include <vector>
#include <vulkan/vulkan.h>

class Chunk
{
public:
    // Dimensions
    static const int SIZE_X = 16;
    static const int SIZE_Y = 16;
    static const int SIZE_Z = 16;

    Chunk(int worldX, int worldY, int worldZ);
    ~Chunk();

    // Basic block access
    int  getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, int voxelID);

    // Dirty flag for partial re-meshing
    bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }
    void markDirty() { m_dirty = true; }

    // World coords
    int worldX() const { return m_worldX; }
    int worldY() const { return m_worldY; }
    int worldZ() const { return m_worldZ; }

    // GPU Buffers & Memory
    VkBuffer       getVertexBuffer() const { return m_vertexBuffer; }
    VkBuffer       getIndexBuffer()  const { return m_indexBuffer; }
    VkDeviceMemory getVertexMemory() const { return m_vertexMemory; }
    VkDeviceMemory getIndexMemory()  const { return m_indexMemory; }

    void setVertexBuffer(VkBuffer vb) { m_vertexBuffer = vb; }
    void setIndexBuffer(VkBuffer ib) { m_indexBuffer = ib; }
    void setVertexMemory(VkDeviceMemory mem) { m_vertexMemory = mem; }
    void setIndexMemory(VkDeviceMemory mem) { m_indexMemory = mem; }

    // Index count for draw calls
    uint32_t getIndexCount() const { return m_indexCount; }
    void setIndexCount(uint32_t c) { m_indexCount = c; }
    uint32_t getVertexCount() const { return m_vertexCount; }
    void setVertexCount(uint32_t c) { m_vertexCount = c; }

private:
    int m_worldX, m_worldY, m_worldZ;
    std::vector<int> m_blocks;
    bool m_dirty = true;
    uint32_t m_vertexCount = 0; // new


    // GPU data per chunk
    VkBuffer       m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer       m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
    uint32_t       m_indexCount = 0;
};