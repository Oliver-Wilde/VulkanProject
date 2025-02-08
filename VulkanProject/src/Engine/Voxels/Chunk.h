#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>  // <-- for glm::vec3 bounding box

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class Chunk
{
public:
    // -----------------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------------
    static const int SIZE_X = 16;
    static const int SIZE_Y = 16;
    static const int SIZE_Z = 16;

    // -----------------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------------
    Chunk(int worldX, int worldY, int worldZ);
    ~Chunk();

    // -----------------------------------------------------------------------------
    // Block Access Methods
    // -----------------------------------------------------------------------------
    int  getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, int voxelID);

    // -----------------------------------------------------------------------------
    // Dirty Flag Management
    // -----------------------------------------------------------------------------
    bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }
    void markDirty() { m_dirty = true; }

    // -----------------------------------------------------------------------------
    // World Coordinates
    // -----------------------------------------------------------------------------
    int worldX() const { return m_worldX; }
    int worldY() const { return m_worldY; }
    int worldZ() const { return m_worldZ; }

    // -----------------------------------------------------------------------------
    // GPU Buffers & Memory (Per-Chunk)
    // -----------------------------------------------------------------------------
    VkBuffer       getVertexBuffer() const { return m_vertexBuffer; }
    VkBuffer       getIndexBuffer()  const { return m_indexBuffer; }
    VkDeviceMemory getVertexMemory() const { return m_vertexMemory; }
    VkDeviceMemory getIndexMemory()  const { return m_indexMemory; }

    void setVertexBuffer(VkBuffer vb) { m_vertexBuffer = vb; }
    void setIndexBuffer(VkBuffer ib) { m_indexBuffer = ib; }
    void setVertexMemory(VkDeviceMemory m) { m_vertexMemory = m; }
    void setIndexMemory(VkDeviceMemory m) { m_indexMemory = m; }

    // -----------------------------------------------------------------------------
    // Draw Information
    // -----------------------------------------------------------------------------
    uint32_t getIndexCount()  const { return m_indexCount; }
    void     setIndexCount(uint32_t c) { m_indexCount = c; }

    uint32_t getVertexCount() const { return m_vertexCount; }
    void     setVertexCount(uint32_t c) { m_vertexCount = c; }

    // -----------------------------------------------------------------------------
    // Bounding Box (for Frustum Culling)
    // -----------------------------------------------------------------------------
    /**
     * Computes the axis-aligned bounding box of this chunk in world-space.
     * @param outMin [out] The minimum corner (x_min, y_min, z_min).
     * @param outMax [out] The maximum corner (x_max, y_max, z_max).
     */
    void getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const;

private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    int m_worldX, m_worldY, m_worldZ; ///< The chunk's position in chunk-space

    // A 1D array holding voxel IDs for each position (x, y, z).
    std::vector<int> m_blocks;
    bool m_dirty = true;

    // Vulkan objects for the chunk's mesh
    VkBuffer       m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer       m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;

    // Vertex/Index counts for rendering
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount = 0;
};
