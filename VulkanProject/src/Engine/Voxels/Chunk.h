#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <vector>
#include <vulkan/vulkan.h>

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
    /**
     * Constructs a Chunk at the specified world coordinates (worldX, worldY, worldZ).
     *
     * @param worldX The X coordinate in chunk-space.
     * @param worldY The Y coordinate in chunk-space.
     * @param worldZ The Z coordinate in chunk-space.
     */
    Chunk(int worldX, int worldY, int worldZ);

    /**
     * Destructor. Cleans up the Chunk data (but not necessarily the Vulkan buffers).
     */
    ~Chunk();

    // -----------------------------------------------------------------------------
    // Block Access Methods
    // -----------------------------------------------------------------------------
    /**
     * Retrieves the voxel ID at the specified local position within the chunk.
     * @param x Local X coordinate [0..SIZE_X-1]
     * @param y Local Y coordinate [0..SIZE_Y-1]
     * @param z Local Z coordinate [0..SIZE_Z-1]
     * @return The ID of the voxel block, or -1 if out of bounds.
     */
    int getBlock(int x, int y, int z) const;

    /**
     * Sets the voxel ID at the specified local position within the chunk.
     * @param x Local X coordinate [0..SIZE_X-1]
     * @param y Local Y coordinate [0..SIZE_Y-1]
     * @param z Local Z coordinate [0..SIZE_Z-1]
     * @param voxelID The ID of the voxel to place.
     */
    void setBlock(int x, int y, int z, int voxelID);

    // -----------------------------------------------------------------------------
    // Dirty Flag Management
    // -----------------------------------------------------------------------------
    /**
     * @return True if the chunk data has changed and needs re-meshing.
     */
    bool isDirty() const { return m_dirty; }

    /**
     * Marks the chunk as no longer dirty (after meshing).
     */
    void clearDirty() { m_dirty = false; }

    /**
     * Marks the chunk as dirty (requires re-meshing).
     */
    void markDirty() { m_dirty = true; }

    // -----------------------------------------------------------------------------
    // World Coordinates
    // -----------------------------------------------------------------------------
    /**
     * @return The X coordinate of the chunk in chunk-space.
     */
    int worldX() const { return m_worldX; }

    /**
     * @return The Y coordinate of the chunk in chunk-space.
     */
    int worldY() const { return m_worldY; }

    /**
     * @return The Z coordinate of the chunk in chunk-space.
     */
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
    void setVertexMemory(VkDeviceMemory mem) { m_vertexMemory = mem; }
    void setIndexMemory(VkDeviceMemory mem) { m_indexMemory = mem; }

    // -----------------------------------------------------------------------------
    // Draw Information
    // -----------------------------------------------------------------------------
    /**
     * @return The total number of indices in the chunk's mesh.
     */
    uint32_t getIndexCount() const { return m_indexCount; }

    /**
     * Sets the number of indices in the chunk's mesh.
     * @param c The new index count.
     */
    void setIndexCount(uint32_t c) { m_indexCount = c; }

    /**
     * @return The total number of vertices in the chunk's mesh.
     */
    uint32_t getVertexCount() const { return m_vertexCount; }

    /**
     * Sets the number of vertices in the chunk's mesh.
     * @param c The new vertex count.
     */
    void setVertexCount(uint32_t c) { m_vertexCount = c; }

private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    int m_worldX, m_worldY, m_worldZ; ///< The chunk's position in chunk-space

    /**
     * A 1D array holding voxel IDs for each position.
     * Size: SIZE_X * SIZE_Y * SIZE_Z.
     */
    std::vector<int> m_blocks;

    /**
     * Indicates whether the chunk data has changed (and thus needs re-meshing).
     */
    bool m_dirty = true;

    /**
     * The total number of vertices in the chunk's mesh.
     */
    uint32_t m_vertexCount = 0;

    // Vulkan objects for the chunk's mesh
    VkBuffer       m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer       m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
    uint32_t       m_indexCount = 0;
};
