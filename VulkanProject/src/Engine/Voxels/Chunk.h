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

/**
 * For advanced LOD transitions, we may store a "seam mesh" or "stitch"
 * geometry that bridges the difference between this chunk’s LOD and
 * a neighbor chunk's LOD.
 *
 * We'll keep one for each face (+X, -X, +Y, -Y, +Z, -Z).
 */
struct ChunkSeamData {
    VkBuffer       seamVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory seamVertexMemory = VK_NULL_HANDLE;
    VkBuffer       seamIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory seamIndexMemory = VK_NULL_HANDLE;
    uint32_t       vertexCount = 0;
    uint32_t       indexCount = 0;
    bool           valid = false;
    // Could also store neighbor info, LOD difference, etc.
};

/**
 * The Chunk class stores voxel data, GPU buffers for multiple LODs,
 * plus optional seam geometry for stitching to neighbors at a different LOD.
 */
class Chunk
{
public:
    static const int SIZE_X = 16;
    static const int SIZE_Y = 16;
    static const int SIZE_Z = 16;

    // Example: 3 LOD levels => LOD0 = full, LOD1/LOD2 = downsampled, etc.
    static const int MAX_LOD_LEVELS = 3;

    /**
     * For convenience, define 6 directions for possible seams.
     * +X=0, -X=1, +Y=2, -Y=3, +Z=4, -Z=5
     */
    enum SeamDirection
    {
        SEAM_POS_X = 0,
        SEAM_NEG_X,
        SEAM_POS_Y,
        SEAM_NEG_Y,
        SEAM_POS_Z,
        SEAM_NEG_Z
    };

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
    bool isLODDirty(int level) const { return m_lodDirty[level]; }
    void markLODDirty(int level) { m_lodDirty[level] = true; }
    void clearLODDirty(int level) { m_lodDirty[level] = false; }

    void markAllLODsDirty(); // Called if chunk data changes

    // (Legacy synonyms for LOD0)
    bool isDirty() const { return m_lodDirty[0]; }
    void clearDirty() { m_lodDirty[0] = false; }
    void markDirty() { m_lodDirty[0] = true; }

    // ---------------------------------------------------
    // Uploading Flag
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
    // Seam Data for LOD Stitching
    // ---------------------------------------------------
    /**
     * Each chunk face can have a small "seam mesh" bridging LOD differences
     * with a neighbor. We keep 6 possible directions.
     */
    ChunkSeamData& getSeamData(SeamDirection dir) { return m_seams[dir]; }
    const ChunkSeamData& getSeamData(SeamDirection dir) const { return m_seams[dir]; }

    /**
     * We might track whether a seam is dirty for each face
     * (e.g. if the neighbor LOD changed).
     */
    void markSeamDirty(SeamDirection dir) { m_seamDirty[dir] = true; }
    void clearSeamDirty(SeamDirection dir) { m_seamDirty[dir] = false; }
    bool isSeamDirty(SeamDirection dir) const { return m_seamDirty[dir]; }

    /**
     * If you want to mark all seams dirty (rare, but possible).
     */
    void markAllSeamsDirty()
    {
        for (int i = 0; i < 6; i++)
            m_seamDirty[i] = true;
    }

    // ---------------------------------------------------
    // Single-LOD Access (backward-compatible)
    // ---------------------------------------------------
    VkBuffer       getVertexBuffer() const { return m_lods[0].vertexBuffer; }
    VkDeviceMemory getVertexMemory() const { return m_lods[0].vertexMemory; }
    VkBuffer       getIndexBuffer()  const { return m_lods[0].indexBuffer; }
    VkDeviceMemory getIndexMemory()  const { return m_lods[0].indexMemory; }
    uint32_t       getVertexCount()  const { return m_lods[0].vertexCount; }
    uint32_t       getIndexCount()   const { return m_lods[0].indexCount; }

    void setVertexBuffer(VkBuffer vb) { m_lods[0].vertexBuffer = vb; }
    void setVertexMemory(VkDeviceMemory mem) { m_lods[0].vertexMemory = mem; }
    void setIndexBuffer(VkBuffer ib) { m_lods[0].indexBuffer = ib; }
    void setIndexMemory(VkDeviceMemory mem) { m_lods[0].indexMemory = mem; }
    void setVertexCount(uint32_t c) { m_lods[0].vertexCount = c; }
    void setIndexCount(uint32_t c) { m_lods[0].indexCount = c; }

    // ---------------------------------------------------
    // Bounding Box & Stats
    // ---------------------------------------------------
    void getBoundingBox(glm::vec3& outMin, glm::vec3& outMax) const;
    std::pair<size_t, size_t> getVoxelUsage() const;

private:
    int m_worldX = 0, m_worldY = 0, m_worldZ = 0;
    std::vector<int> m_blocks; // The chunk’s voxel data

    bool m_isUploading = false;

    // LOD data for up to 3 levels
    ChunkLODData m_lods[MAX_LOD_LEVELS];
    bool         m_lodDirty[MAX_LOD_LEVELS] = { true, true, true };

    // For advanced stitching: up to 6 possible seam meshes (each face).
    ChunkSeamData m_seams[6];
    bool          m_seamDirty[6] = { false, false, false, false, false, false };
};
