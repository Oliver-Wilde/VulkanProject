#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class VulkanContext;
struct Vertex;

/**
 * ResourceManager manages Vulkan resources such as buffers, memory, and shaders.
 * It handles creating/destroying chunk vertex/index buffers, as well as staging
 * buffers for data uploads.
 */
class ResourceManager
{
public:
    ResourceManager(VulkanContext* context);
    ~ResourceManager();

    // --------------------------------------------------------------------------
    // Shader Modules
    // --------------------------------------------------------------------------
    /**
     * Loads a SPIR-V shader module from file if not already loaded.
     * Returns the VkShaderModule handle.
     */
    VkShaderModule loadShaderModule(const std::string& filePath);

    // --------------------------------------------------------------------------
    // Chunk Buffer Creation & Destruction
    // --------------------------------------------------------------------------
    /**
     * Allocates device-local buffers for vertices and indices, then
     * copies data into them using a staging buffer. The resulting
     * buffers and memory handles are returned in outVertexBuffer, outVertexMemory,
     * outIndexBuffer, outIndexMemory.
     */
    void createChunkBuffers(
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds,
        VkBuffer& outVertexBuffer,
        VkDeviceMemory& outVertexMemory,
        VkBuffer& outIndexBuffer,
        VkDeviceMemory& outIndexMemory
    );

    /**
     * Frees the specified vertex and index buffers, along with their memory.
     * If the handles are VK_NULL_HANDLE, does nothing.
     */
    void destroyChunkBuffers(
        VkBuffer vb,
        VkDeviceMemory vbMem,
        VkBuffer ib,
        VkDeviceMemory ibMem
    );

private:
    // --------------------------------------------------------------------------
    // Internal Helpers
    // --------------------------------------------------------------------------
    /**
     * readFile: loads binary data from disk, used for loading SPIR-V.
     */
    std::vector<char> readFile(const std::string& filePath);

    /**
     * createBuffer: wraps vkCreateBuffer + vkAllocateMemory + vkBindBufferMemory.
     */
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory
    );

    /**
     * copyBuffer: does a single, direct copy from src to dst buffer of 'size' bytes,
     * using a temporary command buffer and fence. One region only.
     */
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);

    /**
     * copyBufferRegions: same approach as copyBuffer but for multiple
     * VkBufferCopy regions at once.
     */
    void copyBufferRegions(
        VkBuffer src,
        VkBuffer dst,
        const VkBufferCopy* regions,
        uint32_t regionCount
    );

    /**
     * findMemoryType: picks an appropriate memory type index given
     * memory requirements and desired properties.
     */
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

    /**
     * createStagingBuffer: creates or resizes (if needed) a single staging buffer
     * of at least 'size' bytes, storing it in m_stagingBuffer / m_stagingMemory.
     */
    void createStagingBuffer(VkDeviceSize size);

    /**
     * getOrCreateStagingBuffer: returns m_stagingBuffer / m_stagingMemory,
     * ensuring it is large enough for 'size' bytes.
     */
    std::pair<VkBuffer, VkDeviceMemory> getOrCreateStagingBuffer(VkDeviceSize size);

private:
    VulkanContext* m_context;
    std::unordered_map<std::string, VkShaderModule> m_shaderModules;

    // --------------------------------------------------------------------------
    // Single Reusable Staging Buffer (Phase 4 enhancement)
    // --------------------------------------------------------------------------
    VkBuffer       m_stagingBuffer = VK_NULL_HANDLE; ///< Single staging buffer handle
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE; ///< Memory for staging buffer
    VkDeviceSize   m_stagingBufferSize = 0;             ///< Current capacity of staging buffer
};
