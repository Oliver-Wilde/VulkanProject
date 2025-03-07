#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declaration:
class VulkanContext;

// If your Vertex is declared in a header called "IMesher.h", include it or forward declare
struct Vertex;

class ResourceManager
{
public:
    ResourceManager(VulkanContext* context);
    ~ResourceManager();

    // Existing
    VkShaderModule loadShaderModule(const std::string& filePath);

    // New methods for chunk buffers
    void createChunkBuffers(
        const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds,
        VkBuffer& outVertexBuffer,
        VkDeviceMemory& outVertexMemory,
        VkBuffer& outIndexBuffer,
        VkDeviceMemory& outIndexMemory
    );

    void destroyChunkBuffers(
        VkBuffer vb,
        VkDeviceMemory vbMem,
        VkBuffer ib,
        VkDeviceMemory ibMem
    );

private:
    VulkanContext* m_context;
    std::unordered_map<std::string, VkShaderModule> m_shaderModules;

    std::vector<char> readFile(const std::string& filePath);

    // Private helpers for buffer creation
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory
    );

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);
};
