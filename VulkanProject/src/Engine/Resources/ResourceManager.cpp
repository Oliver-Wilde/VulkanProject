#include "ResourceManager.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Voxels/Meshing/IMesher.h"

#include <fstream>
#include <stdexcept>
#include <cstring> // for memcpy
#include <iostream>
#include <atomic>  // for atomic usage in global memory counter
#include <Engine/Utils/Logger.h>

// A global/static atomic to track total GPU buffer usage:
static std::atomic<size_t> g_totalGPUBufferBytes(0);

static const VkDeviceSize DEFAULT_STAGING_SIZE = 4 * 1024 * 1024; // 4MB default

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

    // Destroy any leftover staging buffer
    if (m_stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_context->getDevice(), m_stagingBuffer, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
    }
    if (m_stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_context->getDevice(), m_stagingMemory, nullptr);
        m_stagingMemory = VK_NULL_HANDLE;
    }
}

// -------------------------------------------
// createStagingBuffer
// -------------------------------------------
void ResourceManager::createStagingBuffer(VkDeviceSize size)
{
    if (m_stagingBuffer != VK_NULL_HANDLE) {
        // If it's already big enough, do nothing
        if (size <= m_stagingBufferSize) {
            return;
        }
        // Otherwise, free and recreate a larger one
        vkDestroyBuffer(m_context->getDevice(), m_stagingBuffer, nullptr);
        vkFreeMemory(m_context->getDevice(), m_stagingMemory, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
        m_stagingMemory = VK_NULL_HANDLE;
        m_stagingBufferSize = 0;
    }

    // Create new staging buffer
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &bufInfo, nullptr, &m_stagingBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create staging buffer!");
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_context->getDevice(), m_stagingBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        memReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &m_stagingMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate staging buffer memory!");
    }

    vkBindBufferMemory(m_context->getDevice(), m_stagingBuffer, m_stagingMemory, 0);
    m_stagingBufferSize = size;
}

// -------------------------------------------
// getOrCreateStagingBuffer
// -------------------------------------------
std::pair<VkBuffer, VkDeviceMemory> ResourceManager::getOrCreateStagingBuffer(VkDeviceSize size)
{
    // Round up size to reduce frequent expansions
    static const VkDeviceSize chunkSize = 512 * 1024; // 512KB increments
    if (size > 0) {
        VkDeviceSize newSize = ((size + chunkSize - 1) / chunkSize) * chunkSize;
        createStagingBuffer(newSize);
    }
    else {
        createStagingBuffer(chunkSize);
    }
    return { m_stagingBuffer, m_stagingMemory };
}

// -------------------------------------------
// readFile => same as original
// -------------------------------------------
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

// -------------------------------------------
// loadShaderModule => same as original
// -------------------------------------------
VkShaderModule ResourceManager::loadShaderModule(const std::string& filePath)
{
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

// -------------------------------------------
// createChunkBuffers => uses single staging
//  => Now gracefully handles copy failures
// -------------------------------------------
bool ResourceManager::createChunkBuffers(
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds,
    VkBuffer& outVertexBuffer,
    VkDeviceMemory& outVertexMemory,
    VkBuffer& outIndexBuffer,
    VkDeviceMemory& outIndexMemory)
{
    // We'll return a bool to indicate success/failure,
    // so the caller knows if it worked or not.

    // Calculate sizes
    VkDeviceSize vbSize = sizeof(Vertex) * verts.size();
    VkDeviceSize ibSize = sizeof(uint32_t) * inds.size();

    // 1) Create device-local buffers
    //    (No changes here)
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

    // 2) staging
    VkDeviceSize totalSize = vbSize + ibSize;
    auto stagingPair = getOrCreateStagingBuffer(totalSize);
    VkBuffer stagingBuf = stagingPair.first;
    VkDeviceMemory stagingMem = stagingPair.second;

    // 3) Copy CPU data into staging
    VkDeviceSize vbOffset = 0;
    VkDeviceSize ibOffset = vbSize;
    if (vbSize > 0) {
        void* data = nullptr;
        vkMapMemory(m_context->getDevice(), stagingMem, vbOffset, vbSize, 0, &data);
        std::memcpy(data, verts.data(), static_cast<size_t>(vbSize));
        vkUnmapMemory(m_context->getDevice(), stagingMem);
    }
    if (ibSize > 0) {
        void* data = nullptr;
        vkMapMemory(m_context->getDevice(), stagingMem, ibOffset, ibSize, 0, &data);
        std::memcpy(data, inds.data(), static_cast<size_t>(ibSize));
        vkUnmapMemory(m_context->getDevice(), stagingMem);
    }

    // 4) Attempt the copy with a try/catch block
    try
    {
        // Copy from staging => device-local vertex buffer
        if (vbSize > 0)
        {
            VkBufferCopy copyV{};
            copyV.srcOffset = vbOffset;
            copyV.dstOffset = 0;
            copyV.size = vbSize;
            copyBufferRegions(stagingBuf, outVertexBuffer, &copyV, 1); // can throw
        }
        if (ibSize > 0)
        {
            VkBufferCopy copyI{};
            copyI.srcOffset = ibOffset;
            copyI.dstOffset = 0;
            copyI.size = ibSize;
            copyBufferRegions(stagingBuf, outIndexBuffer, &copyI, 1);  // can throw
        }
    }
    catch (const std::exception& e)
    {
        // Something failed inside copyBufferRegions (likely vkQueueSubmit).
        // So we must clean up the newly created buffers to avoid a GPU leak.

        std::cerr << "[ResourceManager] createChunkBuffers: Caught exception: "
            << e.what() << std::endl;

        // We allocated these, so let's destroy them so they're not left behind:
        if (outVertexBuffer != VK_NULL_HANDLE || outVertexMemory != VK_NULL_HANDLE)
        {
            destroyChunkBuffers(outVertexBuffer, outVertexMemory,
                VK_NULL_HANDLE, VK_NULL_HANDLE);
            outVertexBuffer = VK_NULL_HANDLE;
            outVertexMemory = VK_NULL_HANDLE;
        }
        if (outIndexBuffer != VK_NULL_HANDLE || outIndexMemory != VK_NULL_HANDLE)
        {
            destroyChunkBuffers(VK_NULL_HANDLE, VK_NULL_HANDLE,
                outIndexBuffer, outIndexMemory);
            outIndexBuffer = VK_NULL_HANDLE;
            outIndexMemory = VK_NULL_HANDLE;
        }

        // Instead of rethrowing, we just log and return false:
        return false;
    }

    // If we reach here, everything succeeded:
    return true;
}

// -------------------------------------------
// destroyChunkBuffers => same as original, but track memory
// -------------------------------------------
void ResourceManager::destroyChunkBuffers(
    VkBuffer vb, VkDeviceMemory vbMem,
    VkBuffer ib, VkDeviceMemory ibMem)
{
    // Log them for debugging
    Logger::Info("destroyChunkBuffers => vb=" + std::to_string((uint64_t)vb) +
        " vbMem=" + std::to_string((uint64_t)vbMem) +
        " ib=" + std::to_string((uint64_t)ib) +
        " ibMem=" + std::to_string((uint64_t)ibMem));

    if (vb != VK_NULL_HANDLE)
    {
        // Also log if vkGetBufferMemoryRequirements fails
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_context->getDevice(), vb, &memReq);

        g_totalGPUBufferBytes.fetch_sub(memReq.size, std::memory_order_relaxed);

        vkDestroyBuffer(m_context->getDevice(), vb, nullptr);
        vb = VK_NULL_HANDLE;
    }
    if (vbMem != VK_NULL_HANDLE)
    {
        vkFreeMemory(m_context->getDevice(), vbMem, nullptr);
        vbMem = VK_NULL_HANDLE;
    }

    if (ib != VK_NULL_HANDLE)
    {
        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_context->getDevice(), ib, &memReq);

        g_totalGPUBufferBytes.fetch_sub(memReq.size, std::memory_order_relaxed);

        vkDestroyBuffer(m_context->getDevice(), ib, nullptr);
        ib = VK_NULL_HANDLE;
    }
    if (ibMem != VK_NULL_HANDLE)
    {
        vkFreeMemory(m_context->getDevice(), ibMem, nullptr);
        ibMem = VK_NULL_HANDLE;
    }
}

// -------------------------------------------
// createBuffer
// -------------------------------------------
void ResourceManager::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &bufInfo, nullptr, &buffer) != VK_SUCCESS) {
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

    // Increase global GPU usage
    g_totalGPUBufferBytes.fetch_add(memReq.size, std::memory_order_relaxed);
}

// -------------------------------------------
// copyBuffer
// -------------------------------------------
void ResourceManager::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    if (size == 0) {
        return;
    }

    VkCommandPool   cmdPool = m_context->getCommandPool();
    VkQueue         gfxQueue = m_context->getGraphicsQueue();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = cmdPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    if (vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo, &cmdBuf) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate staging command buffer!");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;

    vkCmdCopyBuffer(cmdBuf, src, dst, 1, &copyRegion);

    vkEndCommandBuffer(cmdBuf);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence copyFence;
    if (vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr, &copyFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fence for copyBuffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    if (vkQueueSubmit(gfxQueue, 1, &submitInfo, copyFence) != VK_SUCCESS) {
        vkDestroyFence(m_context->getDevice(), copyFence, nullptr);
        throw std::runtime_error("Failed to submit copy command buffer!");
    }

    vkWaitForFences(m_context->getDevice(), 1, &copyFence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_context->getDevice(), copyFence, nullptr);

    vkFreeCommandBuffers(m_context->getDevice(), cmdPool, 1, &cmdBuf);
}

// -------------------------------------------
// copyBufferRegions
// -------------------------------------------
void ResourceManager::copyBufferRegions(
    VkBuffer src,
    VkBuffer dst,
    const VkBufferCopy* regions,
    uint32_t regionCount)
{
    if (!regions || regionCount == 0) {
        return;
    }

    VkCommandPool cmdPool = m_context->getCommandPool();
    VkQueue       gfxQueue = m_context->getGraphicsQueue();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = cmdPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    if (vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo, &cmdBuf) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffer!");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    vkCmdCopyBuffer(cmdBuf, src, dst, regionCount, regions);

    vkEndCommandBuffer(cmdBuf);

    // Fence
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence copyFence;
    if (vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr, &copyFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create fence for copyBuffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    if (vkQueueSubmit(gfxQueue, 1, &submitInfo, copyFence) != VK_SUCCESS) {
        vkDestroyFence(m_context->getDevice(), copyFence, nullptr);
        throw std::runtime_error("Failed to submit copy command buffer!");
    }

    vkWaitForFences(m_context->getDevice(), 1, &copyFence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_context->getDevice(), copyFence, nullptr);

    vkFreeCommandBuffers(m_context->getDevice(), cmdPool, 1, &cmdBuf);
}

// -------------------------------------------
// findMemoryType => unchanged from original
// -------------------------------------------
uint32_t ResourceManager::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        bool isRequiredType = (filter & (1 << i)) != 0;
        bool hasProperties = ((memProps.memoryTypes[i].propertyFlags & props) == props);
        if (isRequiredType && hasProperties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

// A simple getter so the UI or other code can read total GPU usage
size_t ResourceManager::GetTotalGPUBufferBytes() const
{
    return g_totalGPUBufferBytes.load(std::memory_order_relaxed);
}
