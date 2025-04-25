// ============================================================================
// ResourceManager.cpp     (FULL FILE – async uploads ready, *blocking paths no
// longer stall the whole device*)
// ============================================================================

#include "ResourceManager.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Voxels/Meshing/IMesher.h"
#include "Engine/Utils/CpuProfiler.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <atomic>
#include <queue>
#include <mutex>
#include <functional>
#include <numeric>        // std::accumulate
#undef max
// ─────────────────────────────────────────────────────────────────────────────
// Global GPU-memory counter (visible via GetTotalGPUBufferBytes())
// ─────────────────────────────────────────────────────────────────────────────
static std::atomic<size_t> g_totalGPUBufferBytes(0);

// ─────────────────────────────────────────────────────────────────────────────
// Lightweight async-upload support
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



// ============================================================================
// Constants & ctor / dtor
// ============================================================================
static const VkDeviceSize DEFAULT_STAGING_SIZE = 4 * 1024 * 1024;   // 4 MiB

ResourceManager::ResourceManager(VulkanContext* ctx)
    : m_context(ctx)
{
    VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.queueFamilyIndex = ctx->getGraphicsQueueFamilyIndex();
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(ctx->getDevice(), &pci, nullptr, &m_transferPool) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager: transfer pool create failed");
}

ResourceManager::~ResourceManager()
{
    flushUploads(true);                  // ensure async work finished

    for (auto& kv : m_shaderModules)
        vkDestroyShaderModule(m_context->getDevice(), kv.second, nullptr);

    if (m_stagingBuffer)
    {
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(m_context->getDevice(), m_stagingBuffer, &req);
        g_totalGPUBufferBytes.fetch_sub(req.size, std::memory_order_relaxed);
        vkDestroyBuffer(m_context->getDevice(), m_stagingBuffer, nullptr);
    }
    if (m_stagingMemory)
        vkFreeMemory(m_context->getDevice(), m_stagingMemory, nullptr);

    for (VkCommandBuffer c : m_freeCmdBuffers)
        vkFreeCommandBuffers(m_context->getDevice(), m_transferPool, 1, &c);
    for (VkFence f : m_freeFences)
        vkDestroyFence(m_context->getDevice(), f, nullptr);

    vkDestroyCommandPool(m_context->getDevice(), m_transferPool, nullptr);
}

VkCommandBuffer ResourceManager::acquireCmd()
{
    if (!m_freeCmdBuffers.empty())
    {
        VkCommandBuffer cmd = m_freeCmdBuffers.back();
        m_freeCmdBuffers.pop_back();
        vkResetCommandBuffer(cmd, 0);
        return cmd;
    }

    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = m_transferPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(m_context->getDevice(), &ai, &cmd) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager: CMD alloc failed");
    return cmd;
}
void ResourceManager::recycleCmd(VkCommandBuffer cmd)
{
    if (cmd) m_freeCmdBuffers.push_back(cmd);
}

VkFence ResourceManager::acquireFence()
{
    if (!m_freeFences.empty())
    {
        VkFence f = m_freeFences.back();
        m_freeFences.pop_back();
        vkResetFences(m_context->getDevice(), 1, &f);
        return f;
    }

    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence f;
    if (vkCreateFence(m_context->getDevice(), &fi, nullptr, &f) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager: fence create failed");
    return f;
}
void ResourceManager::recycleFence(VkFence f)
{
    if (f) m_freeFences.push_back(f);
}



// ============================================================================
// Staging-buffer helpers
// ============================================================================
void ResourceManager::createStagingBuffer(VkDeviceSize size)
{

    CpuProfiler::ScopedTimer stagingBufferTimer("ResourceManager::createStagingBuffer");  // Profiling staging buffer creation

    /* already large enough? */
    if (m_stagingBuffer && size <= m_stagingBufferSize) return;

    /* destroy the existing buffer (and update stats) */
    if (m_stagingBuffer)
    {
        VkMemoryRequirements oldReq;
        vkGetBufferMemoryRequirements(m_context->getDevice(),
            m_stagingBuffer, &oldReq);
        g_totalGPUBufferBytes.fetch_sub(oldReq.size, std::memory_order_relaxed);

        vkDestroyBuffer(m_context->getDevice(), m_stagingBuffer, nullptr);
        vkFreeMemory(m_context->getDevice(), m_stagingMemory, nullptr);

        m_stagingBuffer = VK_NULL_HANDLE;
        m_stagingMemory = VK_NULL_HANDLE;
        m_stagingBufferSize = 0;
    }

    /* build new buffer */
    VkBufferCreateInfo bc{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bc.size = size;
    bc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &bc, nullptr,
        &m_stagingBuffer) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager: staging buffer create failed");

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_context->getDevice(),
        m_stagingBuffer, &req);

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(m_context->getDevice(), &ai, nullptr,
        &m_stagingMemory) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager: staging buffer alloc failed");

    vkBindBufferMemory(m_context->getDevice(), m_stagingBuffer,
        m_stagingMemory, 0);

    m_stagingBufferSize = size;
    g_totalGPUBufferBytes.fetch_add(req.size, std::memory_order_relaxed);
}

std::pair<VkBuffer, VkDeviceMemory>
ResourceManager::getOrCreateStagingBuffer(VkDeviceSize size)
{
    static const VkDeviceSize CHUNK = 512 * 1024;   // 512 KiB steps
    VkDeviceSize want = ((size + CHUNK - 1) / CHUNK) * CHUNK;
    createStagingBuffer(std::max(want, DEFAULT_STAGING_SIZE));
    return { m_stagingBuffer, m_stagingMemory };
}




// ============================================================================
// File I/O helpers
// ============================================================================
std::vector<char> ResourceManager::readFile(const std::string& filePath)
{

    CpuProfiler::ScopedTimer fileReadTimer("ResourceManager::readFile");  // Profiling file reading

    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Cannot open file: " + filePath);

    size_t sz = static_cast<size_t>(file.tellg());
    std::vector<char> buf(sz);
    file.seekg(0);
    file.read(buf.data(), sz);
    file.close();
    return buf;
}

VkShaderModule ResourceManager::loadShaderModule(const std::string& path)
{

    CpuProfiler::ScopedTimer shaderLoadTimer("ResourceManager::loadShaderModule");  // Profiling shader module loading

    if (auto it = m_shaderModules.find(path); it != m_shaderModules.end())
        return it->second;

    auto code = readFile(path);

    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule mod;
    if (vkCreateShaderModule(m_context->getDevice(), &ci, nullptr,
        &mod) != VK_SUCCESS)
        throw std::runtime_error("Shader module create failed: " + path);

    return m_shaderModules[path] = mod;
}

// ============================================================================
// Chunk-buffer upload  (synchronous path still present)
// ============================================================================
void ResourceManager::createChunkBuffers(const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds,
    VkBuffer& vb, VkDeviceMemory& vbMem,
    VkBuffer& ib, VkDeviceMemory& ibMem)
{
    // Forward to fully asynchronous path to avoid vkQueueWaitIdle stalls.
    createChunkBuffersAsync(verts, inds, vb, vbMem, ib, ibMem, nullptr);
}
void ResourceManager::createChunkBuffersAsync(const std::vector<Vertex>& verts,
    const std::vector<uint32_t>& inds,
    VkBuffer& vb, VkDeviceMemory& vbMem,
    VkBuffer& ib, VkDeviceMemory& ibMem,
    std::function<void()> onComplete)
{

    CpuProfiler::ScopedTimer chunkBufferAsyncTimer("ResourceManager::createChunkBuffersAsync");  // Profiling async chunk buffer creation

    VkDeviceSize vbSz = sizeof(Vertex) * verts.size();
    VkDeviceSize ibSz = sizeof(uint32_t) * inds.size();

    /* 1) device-local buffers */
    createBuffer(vbSz,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vb, vbMem);

    createBuffer(ibSz,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        ib, ibMem);

    /* 2) stage data */
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

    /* 3) async copies */
    const int copyCount = int((vbSz ? 1 : 0) + (ibSz ? 1 : 0));
    if (!copyCount)
    {
        if (onComplete) onComplete();
        return;
    }

    auto done = std::make_shared<std::atomic<int>>(0);
    auto tick = [done, copyCount, onComplete]()
        {
            if (++(*done) == copyCount && onComplete)
                onComplete();
        };

    if (vbSz)
    {
        VkBufferCopy r{ 0, 0, vbSz };
        copyBufferRegionsAsync(stBuf, vb, &r, 1, tick);
    }
    if (ibSz)
    {
        VkBufferCopy r{ vbSz, 0, ibSz };
        copyBufferRegionsAsync(stBuf, ib, &r, 1, tick);
    }
}

// ============================================================================
// destroyChunkBuffers
// ============================================================================
void ResourceManager::destroyChunkBuffers(VkBuffer vb, VkDeviceMemory vbMem,
    VkBuffer ib, VkDeviceMemory ibMem)
{
    CpuProfiler::ScopedTimer destroyChunkBuffers("ResourceManager::destroyChunkBuffers");  // Profiling async chunk buffer creation


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
// Low-level buffer helper
// ============================================================================
void ResourceManager::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props,
    VkBuffer& buf, VkDeviceMemory& mem)
{
    CpuProfiler::ScopedTimer createBuffer("ResourceManager::createBuffer");  // Profiling async chunk buffer creation


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
// Synchronous copy helpers  – *no vkQueueWaitIdle(): use fence instead*
// ============================================================================
void ResourceManager::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    CpuProfiler::ScopedTimer copyBuffer("ResourceManager::copyBuffer");  // Profiling async chunk buffer creation


    if (!size) return;

    VkCommandPool pool = m_context->getCommandPool();

    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_context->getDevice(), &ai, &cmd);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkBufferCopy r{ 0, 0, size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &r);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    vkCreateFence(m_context->getDevice(), &fi, nullptr, &fence);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, fence);
    vkWaitForFences(m_context->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(m_context->getDevice(), fence, nullptr);
    vkFreeCommandBuffers(m_context->getDevice(), pool, 1, &cmd);
}

void ResourceManager::copyBufferRegions(VkBuffer src, VkBuffer dst,
    const VkBufferCopy* regions, uint32_t count)
{
    CpuProfiler::ScopedTimer copyBufferRegions("ResourceManager::copyBufferRegions");

    if (!regions || !count) return;

    VkCommandPool pool = m_context->getCommandPool();

    VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_context->getDevice(), &ai, &cmd);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    vkCmdCopyBuffer(cmd, src, dst, count, regions);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    vkCreateFence(m_context->getDevice(), &fi, nullptr, &fence);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, fence);
    vkWaitForFences(m_context->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(m_context->getDevice(), fence, nullptr);
    vkFreeCommandBuffers(m_context->getDevice(), pool, 1, &cmd);
}

// ============================================================================
// NEW  – asynchronous helpers (unchanged)
// ============================================================================
void ResourceManager::copyBufferAsync(VkBuffer src, VkBuffer dst, VkDeviceSize sz,
    std::function<void()> cb)
{
    CpuProfiler::ScopedTimer _t("RM::copyBufferAsync");
    if (!sz) { if (cb) cb(); return; }

    VkCommandBuffer cmd = acquireCmd();
    VkFence         fnc = acquireFence();

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy r{ 0,0,sz };
    vkCmdCopyBuffer(cmd, src, dst, 1, &r);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, fnc) != VK_SUCCESS)
        throw std::runtime_error("RM::copyBufferAsync submit failed");

    std::lock_guard<std::mutex> lk(g_pendingMutex);
    g_pending.push({ cmd, fnc, std::move(cb) });
}

void ResourceManager::copyBufferRegionsAsync(VkBuffer src, VkBuffer dst,
    const VkBufferCopy* regions,
    uint32_t regionCount,
    std::function<void()> onComplete)
{
    CpuProfiler::ScopedTimer t("ResourceManager::copyBufferRegionsAsync");
    if (!regions || regionCount == 0)
    {
        if (onComplete) onComplete();
        return;
    }

    VkCommandBuffer cmd = acquireCmd();

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    vkCmdCopyBuffer(cmd, src, dst, regionCount, regions);
    vkEndCommandBuffer(cmd);

    VkFence fence = acquireFence();

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, fence) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager::copyBufferRegionsAsync – submit failed");

    std::lock_guard<std::mutex> g(g_pendingMutex);
    g_pending.push({ cmd, fence, std::move(onComplete) });
}
// ============================================================================
// Misc helpers
// ============================================================================
uint32_t ResourceManager::findMemoryType(uint32_t filter,
    VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("ResourceManager: suitable memory type not found");
}

size_t ResourceManager::GetTotalGPUBufferBytes() const
{
    return g_totalGPUBufferBytes.load(std::memory_order_relaxed);
}

/* Flush async uploads once per-frame */
void ResourceManager::flushUploads(bool block /* = false */)
{
    std::lock_guard<std::mutex> lk(g_pendingMutex);

    while (!g_pending.empty())
    {
        PendingUpload& up = g_pending.front();
        VkResult st = vkGetFenceStatus(m_context->getDevice(), up.fence);

        if (st == VK_NOT_READY && !block) break;
        if (st == VK_NOT_READY && block)
            vkWaitForFences(m_context->getDevice(), 1, &up.fence, VK_TRUE, UINT64_MAX);

        recycleFence(up.fence);
        recycleCmd(up.cmd);

        if (up.onComplete) up.onComplete();
        g_pending.pop();
    }
}




void ResourceManager::trimStagingBuffer()
{
    /* nothing to do if we never allocated one */
    if (!m_stagingBuffer) return;

    /* keep current buffer if ≤ default size */
    if (m_stagingBufferSize <= DEFAULT_STAGING_SIZE) return;

    /* don’t re-allocate while uploads are still pending */
    {
        std::lock_guard<std::mutex> guard(g_pendingMutex);
        if (!g_pending.empty()) return;
    }

    /* recreate at default size (createStagingBuffer handles stats) */
    createStagingBuffer(DEFAULT_STAGING_SIZE);
}