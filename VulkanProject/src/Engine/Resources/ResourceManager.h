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
    /** Loads (or retrieves) a SPIR?V shader module. */
    VkShaderModule loadShaderModule(const std::string& filePath);

    // --------------------------------------------------------------------------
    // Chunk Buffer Creation & Destruction
    // --------------------------------------------------------------------------
    /** Creates device?local buffers and uploads vertex/index data. */
    void createChunkBuffers(
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds,
        VkBuffer& outVertexBuffer,
        VkDeviceMemory& outVertexMemory,
        VkBuffer& outIndexBuffer,
        VkDeviceMemory& outIndexMemory);

    /** Destroys the specified vertex/index buffers and frees their memory. */
    void destroyChunkBuffers(
        VkBuffer vb,
        VkDeviceMemory vbMem,
        VkBuffer ib,
        VkDeviceMemory ibMem);

    /** Returns total GPU bytes currently allocated for buffers (debug info). */
    size_t GetTotalGPUBufferBytes() const;

    // --------------------------------------------------------------------------
    // Low?level copy helpers (now PUBLIC so Renderer can call them)
    // --------------------------------------------------------------------------
    /** Copy an entire range from src ? dst using a staging command buffer. */
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);

    /** Copy multiple VkBufferCopy regions in one go (used by MeshBatch). */
    void copyBufferRegions(
        VkBuffer src,
        VkBuffer dst,
        const VkBufferCopy* regions,
        uint32_t regionCount);

private:
    // --------------------------------------------------------------------------
    // Internal Helpers
    // --------------------------------------------------------------------------
    std::vector<char> readFile(const std::string& filePath);

    /** vkCreateBuffer + memory allocation + bind. */
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory);

    /** Selects a suitable memory type index. */
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

    /** Creates or grows the reusable staging buffer. */
    void createStagingBuffer(VkDeviceSize size);
    std::pair<VkBuffer, VkDeviceMemory> getOrCreateStagingBuffer(VkDeviceSize size);

private:
    VulkanContext* m_context = nullptr;
    std::unordered_map<std::string, VkShaderModule> m_shaderModules;

    // Single reusable staging buffer
    VkBuffer       m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize   m_stagingBufferSize = 0;
};
