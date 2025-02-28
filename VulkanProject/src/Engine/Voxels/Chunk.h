#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>
#include <utility> // for std::pair

/**
 * Holds GPU buffer information for one LOD level.
 * Each LOD can have its own vertex/index buffers and counts.
 */
struct ChunkLODData {
    VkBuffer       vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer       indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
    uint32_t       vertexCount = 0;
    uint32_t       indexCount = 0;
    bool           valid = false; // True if this LOD's mesh is uploaded
};

class Chunk
{
public:
    static const int SIZE_X = 16;
    static const int SIZE_Y = 16;
    static const int SIZE_Z = 16;

    // Example: 3 LOD levels => LOD0 = full res, LOD1 ~ half res, LOD2 ~ quarter, etc.
    static const int MAX_LOD_LEVELS = 3;

    /**
     * Constructor / Destructor
     */
    Chunk(int worldX, int worldY, int worldZ);
    ~Chunk();

    /**
     * Returns the voxel at (x,y,z). Out-of-bounds => -1 or "air."
     */
    int  getBlock(int x, int y, int z) const;

    /**
     * Sets a voxel at (x,y,z). If the value changed, mark all LODs dirty.
     */
    void setBlock(int x, int y, int z, int voxelID);

    /**
     * Provide read-only access to the entire voxel data array.
     */
    const std::vector<int>& getBlocks() const { return m_blocks; }

    // ---------------------------------------------------
    // LOD Dirty Flags
    // ---------------------------------------------------
    /**
     * Check if a specific LOD level is dirty (needs re-meshing).
     */
    bool isLODDirty(int level) const { return m_lodDirty[level]; }

    /**
     * Mark a specific LOD level as dirty.
     */
    void markLODDirty(int level) { m_lodDirty[level] = true; }

    /**
     * Clear the dirty flag for a specific LOD level.
     */
    void clearLODDirty(int level) { m_lodDirty[level] = false; }

    /**
     * Mark *all* LOD levels as dirty. Called when a voxel changes.
     */
    void markAllLODsDirty();

    // ---------------------------------------------------
    // Legacy Dirty Flag (for LOD0)
    // ---------------------------------------------------
    bool isDirty() const { return m_lodDirty[0]; }
    void clearDirty() { m_lodDirty[0] = false; }
    void markDirty() { m_lodDirty[0] = true; }

    // ---------------------------------------------------
    // Uploading Flag (shared across all LODs)
    // ---------------------------------------------------
    bool isUploading() const { return m_isUploading; }
    void setIsUploading(bool b) { m_isUploading = b; }

    // ---------------------------------------------------
    // World Coordinates
    // ---------------------------------------------------
    int worldX() const { return m_worldX; }
    int worldY() const { return m_worldY; }
    int worldZ() const { return m_worldZ; }

    // ---------------------------------------------------
    // Multi-LOD Access
    // ---------------------------------------------------
    /**
     * Returns a reference to the LOD data for a given level, e.g. LOD0, LOD1, etc.
     */
    ChunkLODData& getLODData(int level) { return m_lods[level]; }
    const ChunkLODData& getLODData(int level) const { return m_lods[level]; }

    // ---------------------------------------------------
    // Backward-Compatible Single-LOD Access (LOD0)
    // ---------------------------------------------------
    VkBuffer       getVertexBuffer() const { return m_lods[0].vertexBuffer; }
    VkDeviceMemory getVertexMemory() const { return m_lods[0].vertexMemory; }
    VkBuffer       getIndexBuffer() const { return m_lods[0].indexBuffer; }
    VkDeviceMemory getIndexMemory() const { return m_lods[0].indexMemory; }
    uint32_t       getVertexCount() const { return m_lods[0].vertexCount; }
    uint32_t       getIndexCount() const { return m_lods[0].indexCount; }

    void setVertexBuffer(VkBuffer vb) { m_lods[0].vertexBuffer = vb; }
    void setVertexMemory(VkDeviceMemory mem) { m_lods[0].vertexMemory = mem; }
    void setIndexBuffer(VkBuffer ib) { m_lods[0].indexBuffer = ib; }
    void setIndexMemory(VkDeviceMemory mem) { m_lods[0].indexMemory = mem; }
    void setVertexCount(uint32_t c) { m_lods[0].vertexCount = c; }
    void setIndexCount(uint32_t c) { m_lods[0].indexCount = c; }

    // ---------------------------------------------------
    // Misc: Bounding Box & Stats
    // ---------------------------------------------------
    /**
     * Computes the world-space AABB for frustum culling, etc.
     */
    void getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const;

    /**
     * Returns #active (non-air) voxels, #empty (air) voxels.
     */
    std::pair<size_t, size_t> getVoxelUsage() const;

private:
    // -------------------------------------------------------------------------
    // Member Variables
    // -------------------------------------------------------------------------
    int m_worldX = 0, m_worldY = 0, m_worldZ = 0;

    // The chunk’s voxel data (1D array: x + SIZE_X*(y + SIZE_Y*z)).
    std::vector<int> m_blocks;

    // A flag that indicates if the chunk is uploading new GPU buffers.
    bool m_isUploading = false;

    // For multi-LOD: up to 3 levels stored in an array.
    ChunkLODData m_lods[MAX_LOD_LEVELS];

    // Each LOD level can be marked dirty if the chunk’s data changes.
    bool m_lodDirty[MAX_LOD_LEVELS] = { true, true, true };
};
