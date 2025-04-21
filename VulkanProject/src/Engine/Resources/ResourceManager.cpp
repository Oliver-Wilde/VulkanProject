// ============================================================================
// ResourceManager.cpp     (FULL FILE – async uploads ready)
// ============================================================================

#include "ResourceManager.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Voxels/Meshing/IMesher.h"

#include <fstream>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <atomic>
#include <queue>
#include <mutex>
#include <functional>
#include <numeric>        
// std::accumulate (used by others)

// ─────────────────────────────────────────────────────────────────────────────
// Global GPU‑memory counter (visible via GetTotalGPUBufferBytes())
// ─────────────────────────────────────────────────────────────────────────────
static std::atomic<size_t> g_totalGPUBufferBytes(0);

// ─────────────────────────────────────────────────────────────────────────────
// Lightweight async‑upload support (no header changes required)
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    struct PendingUpload
    {
        VkCommandBuffer       cmd = VK_NULL_HANDLE;
        VkFence               fence = VK_NULL_HANDLE;
        std::function<void()> onComplete;
    };

    std::queue<PendingUpload> g_pending;
    std::mutex                g_pendingMutex;
}

/* Helper – checks & recycles finished uploads */
static void flushPendingUploads(VulkanContext* ctx, bool block)
{
    std::lock_guard<std::mutex> guard(g_pendingMutex);

    while (!g_pending.empty())
    {
        PendingUpload& up = g_pending.front();
        VkResult st = vkGetFenceStatus(ctx->getDevice(), up.fence);

        if (st == VK_NOT_READY && !block) break;
        if (st == VK_NOT_READY && block)
            vkWaitForFences(ctx->getDevice(), 1, &up.fence, VK_TRUE, UINT64_MAX);

        vkDestroyFence(ctx->getDevice(), up.fence, nullptr);
        vkFreeCommandBuffers(ctx->getDevice(), ctx->getCommandPool(), 1, &up.cmd);

        if (up.onComplete) up.onComplete();
        g_pending.pop();
    }
}

// ============================================================================
// Constants & ctor / dtor
// ============================================================================
static const VkDeviceSize DEFAULT_STAGING_SIZE = 4 * 1024 * 1024;   // 4 MiB

ResourceManager::ResourceManager(VulkanContext* context)
    : m_context(context)
{
    /* nothing else */
}

ResourceManager::~ResourceManager()
{
    /* make sure every outstanding upload finished */
    flushPendingUploads(m_context, /*block=*/true);

    for (auto& kv : m_shaderModules)
        vkDestroyShaderModule(m_context->getDevice(), kv.second, nullptr);
    m_shaderModules.clear();

    if (m_stagingBuffer) vkDestroyBuffer(m_context->getDevice(), m_stagingBuffer, nullptr);
    if (m_stagingMemory) vkFreeMemory(m_context->getDevice(), m_stagingMemory, nullptr);
}

// ============================================================================
// Staging‑buffer helpers  (unchanged logic)
// ============================================================================
void ResourceManager::createStagingBuffer(VkDeviceSize size)
{
    if (m_stagingBuffer && size <= m_stagingBufferSize) return;

    if (m_stagingBuffer)
    {
        vkDestroyBuffer(m_context->getDevice(), m_stagingBuffer, nullptr);
        vkFreeMemory(m_context->getDevice(), m_stagingMemory, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
        m_stagingMemory = VK_NULL_HANDLE;
        m_stagingBufferSize = 0;
    }

    VkBufferCreateInfo bc{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bc.size = size;
    bc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_context->getDevice(), &bc, nullptr, &m_stagingBuffer) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager: staging buffer create failed");

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_context->getDevice(), m_stagingBuffer, &req);

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(m_context->getDevice(), &ai, nullptr, &m_stagingMemory) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager: staging buffer alloc failed");

    vkBindBufferMemory(m_context->getDevice(), m_stagingBuffer, m_stagingMemory, 0);
    m_stagingBufferSize = size;
}

std::pair<VkBuffer, VkDeviceMemory> ResourceManager::getOrCreateStagingBuffer(VkDeviceSize size)
{
    static const VkDeviceSize CHUNK = 512 * 1024;                     // 512 KiB steps
    VkDeviceSize want = ((size + CHUNK - 1) / CHUNK) * CHUNK;
    createStagingBuffer(std::max(want, DEFAULT_STAGING_SIZE));
    return { m_stagingBuffer, m_stagingMemory };
}

// ============================================================================
// File I/O helpers (unchanged)
// ============================================================================
std::vector<char> ResourceManager::readFile(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Cannot open file: " + filePath);

    size_t sz = static_cast<size_t>(file.tellg());
    std::vector<char> buf(sz);
    file.seekg(0);  file.read(buf.data(), sz);  file.close();
    return buf;
}

VkShaderModule ResourceManager::loadShaderModule(const std::string& path)
{
    if (auto it = m_shaderModules.find(path); it != m_shaderModules.end())
        return it->second;

    auto code = readFile(path);

    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule mod;
    if (vkCreateShaderModule(m_context->getDevice(), &ci, nullptr, &mod) != VK_SUCCESS)
        throw std::runtime_error("Shader module create failed: " + path);

    return m_shaderModules[path] = mod;
}

// ============================================================================
// Chunk‑buffer upload  (still synchronous, but uses copyBufferRegions)
// ============================================================================
void ResourceManager::createChunkBuffers(const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds,
    VkBuffer& vb, VkDeviceMemory& vbMem,
    VkBuffer& ib, VkDeviceMemory& ibMem)
{
    VkDeviceSize vbSz = sizeof(Vertex) * verts.size();
    VkDeviceSize ibSz = sizeof(uint32_t) * inds.size();

    createBuffer(vbSz,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vb, vbMem);

    createBuffer(ibSz,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        ib, ibMem);

    // stage
    VkDeviceSize total = vbSz + ibSz;
    auto [stBuf, stMem] = getOrCreateStagingBuffer(total);

    // copy CPU → staging
    if (vbSz)
    {
        void* p; vkMapMemory(m_context->getDevice(), stMem, 0, vbSz, 0, &p);
        std::memcpy(p, verts.data(), static_cast<size_t>(vbSz));
        vkUnmapMemory(m_context->getDevice(), stMem);
    }
    if (ibSz)
    {
        void* p; vkMapMemory(m_context->getDevice(), stMem, vbSz, ibSz, 0, &p);
        std::memcpy(p, inds.data(), static_cast<size_t>(ibSz));
        vkUnmapMemory(m_context->getDevice(), stMem);
    }

    // copy staging → device (two regions)
    VkBufferCopy regions[2]{};
    uint32_t n = 0;
    if (vbSz) { regions[n] = { 0, 0, vbSz }; ++n; }
    if (ibSz) { regions[n] = { vbSz, 0, ibSz }; ++n; }

    copyBufferRegions(stBuf, vb, &regions[0], vbSz ? 1 : 0);
    if (ibSz)
        copyBufferRegions(stBuf, ib, &regions[1], 1);
}

void ResourceManager::createChunkBuffersAsync(
    const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds,
    VkBuffer& vb, VkDeviceMemory& vbMem,
    VkBuffer& ib, VkDeviceMemory& ibMem,
    std::function<void()> onComplete)
{
    VkDeviceSize vbSz = sizeof(Vertex) * verts.size();
    VkDeviceSize ibSz = sizeof(uint32_t) * inds.size();

    /* 1) create device‑local buffers */
    createBuffer(vbSz,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vb, vbMem);

    createBuffer(ibSz,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ib, ibMem);

    /* 2) ensure staging buffer large enough & map CPU data */
    VkDeviceSize total = vbSz + ibSz;
    auto [stBuf, stMem] = getOrCreateStagingBuffer(total);

    if (vbSz)
    {
        void* p; vkMapMemory(m_context->getDevice(), stMem, 0, vbSz, 0, &p);
        std::memcpy(p, verts.data(), size_t(vbSz));
        vkUnmapMemory(m_context->getDevice(), stMem);
    }
    if (ibSz)
    {
        void* p; vkMapMemory(m_context->getDevice(), stMem, vbSz, ibSz, 0, &p);
        std::memcpy(p, inds.data(), size_t(ibSz));
        vkUnmapMemory(m_context->getDevice(), stMem);
    }

    /* 3) schedule async copies (one per dst buffer) */
    const int copyCount = int((vbSz ? 1 : 0) + (ibSz ? 1 : 0));
    if (copyCount == 0)
    {
        if (onComplete) onComplete();
        return;
    }

    auto done = std::make_shared<std::atomic<int>>(0);
    auto makeCallback = [done, copyCount, onComplete]()
        {
            if (++(*done) == copyCount && onComplete)
                onComplete();
        };

    if (vbSz)
    {
        VkBufferCopy r{ 0, 0, vbSz };
        copyBufferRegionsAsync(stBuf, vb, &r, 1, makeCallback);
    }
    if (ibSz)
    {
        VkBufferCopy r{ vbSz, 0, ibSz };
        copyBufferRegionsAsync(stBuf, ib, &r, 1, makeCallback);
    }
}

// ============================================================================
// destroyChunkBuffers  (tracks global usage)
// ============================================================================
void ResourceManager::destroyChunkBuffers(VkBuffer vb, VkDeviceMemory vbMem,
    VkBuffer ib, VkDeviceMemory ibMem)
{
    auto destroy = [&](VkBuffer b)
        {
            if (!b) return;
            VkMemoryRequirements r;
            vkGetBufferMemoryRequirements(m_context->getDevice(), b, &r);
            g_totalGPUBufferBytes.fetch_sub(r.size, std::memory_order_relaxed);
            vkDestroyBuffer(m_context->getDevice(), b, nullptr);
        };
    if (vb) destroy(vb);
    if (ib) destroy(ib);
    if (vbMem) vkFreeMemory(m_context->getDevice(), vbMem, nullptr);
    if (ibMem) vkFreeMemory(m_context->getDevice(), ibMem, nullptr);
}

// ============================================================================
// Low‑level buffer helper  (unchanged + global counter)
// ============================================================================
void ResourceManager::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props,
    VkBuffer& buf, VkDeviceMemory& mem)
{
    VkBufferCreateInfo bc{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bc.size = size;
    bc.usage = usage;
    bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_context->getDevice(), &bc, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error("Buffer create failed");

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_context->getDevice(), buf, &req);

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
    if (vkAllocateMemory(m_context->getDevice(), &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("Buffer alloc failed");

    vkBindBufferMemory(m_context->getDevice(), buf, mem, 0);
    g_totalGPUBufferBytes.fetch_add(req.size, std::memory_order_relaxed);
}

// ============================================================================
// Synchronous copy helpers  (UNCHANGED)
// ============================================================================
void ResourceManager::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    if (!size) return;

    VkCommandPool pool = m_context->getCommandPool();
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_context->getDevice(), &ai, &cmd);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkBufferCopy r{ 0,0,size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &r);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;

    vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_context->getGraphicsQueue());

    vkFreeCommandBuffers(m_context->getDevice(), pool, 1, &cmd);
}

void ResourceManager::copyBufferRegions(VkBuffer src, VkBuffer dst,
    const VkBufferCopy* regions, uint32_t count)
{
    if (!regions || !count) return;

    VkCommandPool pool = m_context->getCommandPool();
    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_context->getDevice(), &ai, &cmd);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    vkCmdCopyBuffer(cmd, src, dst, count, regions);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;

    vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_context->getGraphicsQueue());

    vkFreeCommandBuffers(m_context->getDevice(), pool, 1, &cmd);
}

// ============================================================================
// NEW  ── asynchronous helper (no header change needed until you call it)
// ============================================================================
void ResourceManager::copyBufferAsync(VkBuffer src, VkBuffer dst, VkDeviceSize size,
    std::function<void()> onComplete)
{
    if (!size) { if (onComplete) onComplete(); return; }

    VkCommandPool pool = m_context->getCommandPool();

    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(m_context->getDevice(), &ai, &cmd) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager::copyBufferAsync – cmd alloc failed");

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkBufferCopy r{ 0,0,size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &r);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    if (vkCreateFence(m_context->getDevice(), &fi, nullptr, &fence) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager::copyBufferAsync – fence create failed");

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;

    if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, fence) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager::copyBufferAsync – submit failed");

    std::lock_guard<std::mutex> guard(g_pendingMutex);
    g_pending.push({ cmd, fence, std::move(onComplete) });
}

/* call this once per‑frame (e.g. inside Renderer::freeDeferredResources) */
void ResourceManager::flushUploads(bool block /*= false*/)
{
    flushPendingUploads(m_context, block);
}

// ============================================================================
// Misc helpers
// ============================================================================
uint32_t ResourceManager::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;

    throw std::runtime_error("ResourceManager: suitable memory type not found");
}

size_t ResourceManager::GetTotalGPUBufferBytes() const
{
    return g_totalGPUBufferBytes.load(std::memory_order_relaxed);
}


void ResourceManager::copyBufferRegionsAsync(VkBuffer src, VkBuffer dst,
    const VkBufferCopy* regions,
    uint32_t regionCount,
    std::function<void()> onComplete)
{
    if (!regions || regionCount == 0)
    {
        if (onComplete) onComplete();
        return;
    }

    VkCommandPool pool = m_context->getCommandPool();

    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(m_context->getDevice(), &ai, &cmd) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager::copyBufferRegionsAsync – cmd alloc failed");

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    vkCmdCopyBuffer(cmd, src, dst, regionCount, regions);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    if (vkCreateFence(m_context->getDevice(), &fi, nullptr, &fence) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager::copyBufferRegionsAsync – fence create failed");

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, fence) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager::copyBufferRegionsAsync – submit failed");

    std::lock_guard<std::mutex> guard(g_pendingMutex);
    g_pending.push({ cmd, fence, std::move(onComplete) });
}