#include "ResourceManager.h"
#include "Engine/Graphics/VulkanContext.h"
// Include your Vertex struct header (e.g. "IMesher.h") if needed for the Vertex definition
#include "Engine/Voxels/Meshing/IMesher.h"

#include <fstream>
#include <stdexcept>
#include <cstring> // for memcpy

ResourceManager::ResourceManager(VulkanContext* context)
    : m_context(context)
{
}

ResourceManager::~ResourceManager()
{
    // Destroy all loaded shader modules
    for (auto& kv : m_shaderModules) {
        vkDestroyShaderModule(m_context->getDevice(), kv.second, nullptr);
    }
    m_shaderModules.clear();
}

std::vector<char> ResourceManager::readFile(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filePath);
    }
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

VkShaderModule ResourceManager::loadShaderModule(const std::string& filePath)
{
    // Check if this shader is already loaded
    auto it = m_shaderModules.find(filePath);
    if (it != m_shaderModules.end()) {
        return it->second;
    }

    auto code = readFile(filePath);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_context->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module for " + filePath);
    }

    m_shaderModules[filePath] = shaderModule;
    return shaderModule;
}


// -----------------------------------------------------------------------------
// New Methods for Chunk Buffers
// -----------------------------------------------------------------------------

void ResourceManager::createChunkBuffers(
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds,
    VkBuffer& outVertexBuffer,
    VkDeviceMemory& outVertexMemory,
    VkBuffer& outIndexBuffer,
    VkDeviceMemory& outIndexMemory)
{
    // 1) Create final device-local buffers
    VkDeviceSize vbSize = sizeof(Vertex) * verts.size();
    VkDeviceSize ibSize = sizeof(uint32_t) * inds.size();

    createBuffer(
        vbSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        outVertexBuffer,
        outVertexMemory
    );

    createBuffer(
        ibSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        outIndexBuffer,
        outIndexMemory
    );

    // 2) Create staging buffers
    //    (host-visible so we can copy CPU data into them)
    VkBuffer stagingVB = VK_NULL_HANDLE;
    VkDeviceMemory stagingVBMem = VK_NULL_HANDLE;
    createBuffer(
        vbSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingVB,
        stagingVBMem
    );

    VkBuffer stagingIB = VK_NULL_HANDLE;
    VkDeviceMemory stagingIBMem = VK_NULL_HANDLE;
    createBuffer(
        ibSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingIB,
        stagingIBMem
    );

    // 3) Copy CPU data into staging
    //    (If vbSize/ibSize are zero, these loops safely do nothing)
    if (vbSize > 0)
    {
        void* data;
        vkMapMemory(m_context->getDevice(), stagingVBMem, 0, vbSize, 0, &data);
        std::memcpy(data, verts.data(), static_cast<size_t>(vbSize));
        vkUnmapMemory(m_context->getDevice(), stagingVBMem);
    }

    if (ibSize > 0)
    {
        void* data;
        vkMapMemory(m_context->getDevice(), stagingIBMem, 0, ibSize, 0, &data);
        std::memcpy(data, inds.data(), static_cast<size_t>(ibSize));
        vkUnmapMemory(m_context->getDevice(), stagingIBMem);
    }

    // 4) Copy staging buffers into final device-local buffers
    copyBuffer(stagingVB, outVertexBuffer, vbSize);
    copyBuffer(stagingIB, outIndexBuffer, ibSize);

    // 5) Destroy staging resources
    vkDestroyBuffer(m_context->getDevice(), stagingVB, nullptr);
    vkFreeMemory(m_context->getDevice(), stagingVBMem, nullptr);
    vkDestroyBuffer(m_context->getDevice(), stagingIB, nullptr);
    vkFreeMemory(m_context->getDevice(), stagingIBMem, nullptr);
}

void ResourceManager::destroyChunkBuffers(
    VkBuffer vb, VkDeviceMemory vbMem,
    VkBuffer ib, VkDeviceMemory ibMem)
{
    if (vb != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_context->getDevice(), vb, nullptr);
    }
    if (vbMem != VK_NULL_HANDLE) {
        vkFreeMemory(m_context->getDevice(), vbMem, nullptr);
    }
    if (ib != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_context->getDevice(), ib, nullptr);
    }
    if (ibMem != VK_NULL_HANDLE) {
        vkFreeMemory(m_context->getDevice(), ibMem, nullptr);
    }
}


// -----------------------------------------------------------------------------
// Private Helpers for Creating/Copying Buffers
// -----------------------------------------------------------------------------

void ResourceManager::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_context->getDevice(), buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

    if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(m_context->getDevice(), buffer, bufferMemory, 0);
}

void ResourceManager::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    if (size == 0) {
        // No data to copy => skip
        return;
    }

    // 1) Allocate a temporary command buffer from our existing pool
    VkCommandPool   cmdPool = m_context->getCommandPool();
    VkQueue         gfxQueue = m_context->getGraphicsQueue();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = cmdPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo, &cmdBuf);

    // 2) Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    // 3) Copy region
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmdBuf, src, dst, 1, &copyRegion);

    // 4) Submit
    vkEndCommandBuffer(cmdBuf);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    vkQueueSubmit(gfxQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(gfxQueue);

    // 5) Clean up
    vkFreeCommandBuffers(m_context->getDevice(), cmdPool, 1, &cmdBuf);
}

uint32_t ResourceManager::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        bool isRequiredType = (filter & (1 << i)) != 0;
        bool hasProperties = (memProps.memoryTypes[i].propertyFlags & props) == props;
        if (isRequiredType && hasProperties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type for buffer!");
}
