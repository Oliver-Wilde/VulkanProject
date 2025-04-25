// ============================================================================
// ResourceManager.cpp   – 2025-04-25
//   * 3-slot staging-buffer ring (smooth uploads, no realloc stalls)
//   * Fully asynchronous uploads (no vkDeviceWaitIdle / vkQueueWaitIdle)
//   * All helpers referenced by other modules implemented
// ============================================================================

#include "ResourceManager.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Utils/CpuProfiler.h"

#include <fstream>
#include <stdexcept>
#include <cstring>
#include <atomic>
#include <queue>
#include <mutex>
#include <functional>
#include "Engine/Voxels/Meshing/IMesher.h"
#undef max   // windows.h safety

/* ────────────────────────────────────────────────────────────────────────── */
/* Global GPU-memory tracker                                                 */
/* ────────────────────────────────────────────────────────────────────────── */
static std::atomic<size_t> g_totalGPUBufferBytes{ 0 };

/* ────────────────────────────────────────────────────────────────────────── */
/* Async-upload queue                                                        */
/* ────────────────────────────────────────────────────────────────────────── */
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

static const VkDeviceSize DEFAULT_STAGING_SIZE = 4 * 1024 * 1024ULL; // 4 MiB

/* ═════════════════════════════════ ctor / dtor ═══════════════════════════ */
ResourceManager::ResourceManager(VulkanContext* ctx)
    : m_context(ctx)
{
    /* transfer-only command pool (allows command-buffer reset) */
    VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.queueFamilyIndex = ctx->getGraphicsQueueFamilyIndex();
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(ctx->getDevice(), &pci, nullptr, &m_transferPool) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager: transfer pool create failed");

    /* initialise three equally-sized staging buffers */
    for (int i = 0; i < kStagingSlots; ++i)
        ensureSlotCapacity(i, DEFAULT_STAGING_SIZE);
}

ResourceManager::~ResourceManager()
{
    flushUploads(true);                    // wait out any outstanding copies

    /* shader modules */
    for (auto& kv : m_shaderModules)
        vkDestroyShaderModule(m_context->getDevice(), kv.second, nullptr);

    /* staging ring */
    for (int i = 0; i < kStagingSlots; ++i)
    {
        if (m_slots[i].buffer)
        {
            VkMemoryRequirements rq{};
            vkGetBufferMemoryRequirements(m_context->getDevice(),
                m_slots[i].buffer, &rq);
            g_totalGPUBufferBytes.fetch_sub(rq.size, std::memory_order_relaxed);
            vkDestroyBuffer(m_context->getDevice(), m_slots[i].buffer, nullptr);
        }
        if (m_slots[i].memory)
            vkFreeMemory(m_context->getDevice(), m_slots[i].memory, nullptr);
    }

    /* cached command buffers / fences */
    for (VkCommandBuffer c : m_freeCmdBuffers)
        vkFreeCommandBuffers(m_context->getDevice(), m_transferPool, 1, &c);
    for (VkFence f : m_freeFences)
        vkDestroyFence(m_context->getDevice(), f, nullptr);

    vkDestroyCommandPool(m_context->getDevice(), m_transferPool, nullptr);
}

/* ═════════════════════════════ staging-ring helpers ═══════════════════════ */
void ResourceManager::ensureSlotCapacity(int slot, VkDeviceSize want)
{
    StagingSlot& s = m_slots[slot];
    if (s.buffer && want <= s.size) return;            // big enough

    /* destroy old buffer/memory (if any) */
    if (s.buffer)
    {
        VkMemoryRequirements rq{};
        vkGetBufferMemoryRequirements(m_context->getDevice(), s.buffer, &rq);
        g_totalGPUBufferBytes.fetch_sub(rq.size, std::memory_order_relaxed);
        vkDestroyBuffer(m_context->getDevice(), s.buffer, nullptr);
    }
    if (s.memory)
        vkFreeMemory(m_context->getDevice(), s.memory, nullptr);

    /* create new host-visible buffer */
    VkBufferCreateInfo bc{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bc.size = want;
    bc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_context->getDevice(), &bc, nullptr, &s.buffer) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager: staging buffer create failed");

    VkMemoryRequirements rq{};
    vkGetBufferMemoryRequirements(m_context->getDevice(), s.buffer, &rq);

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = rq.size;
    ai.memoryTypeIndex = findMemoryType(rq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(m_context->getDevice(), &ai, nullptr, &s.memory) != VK_SUCCESS)
        throw std::runtime_error("ResourceManager: staging memory alloc failed");

    vkBindBufferMemory(m_context->getDevice(), s.buffer, s.memory, 0);
    s.size = want;
    g_totalGPUBufferBytes.fetch_add(rq.size, std::memory_order_relaxed);
}

inline ResourceManager::StagingSlot& ResourceManager::currentSlot()
{
    return m_slots[m_currentSlot];
}

std::pair<VkBuffer, VkDeviceMemory>
ResourceManager::getOrCreateStagingBuffer(VkDeviceSize size)
{
    static const VkDeviceSize CHUNK = 512 * 1024ULL;            // 512 KiB granularity
    VkDeviceSize want = ((size + CHUNK - 1) / CHUNK) * CHUNK;
    ensureSlotCapacity(int(m_currentSlot),
        std::max(want, DEFAULT_STAGING_SIZE));
    return { currentSlot().buffer, currentSlot().memory };
}

/* ═════════════════════ command-buffer / fence cache ══════════════════════ */
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
void ResourceManager::recycleCmd(VkCommandBuffer c) { if (c) m_freeCmdBuffers.push_back(c); }

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
void ResourceManager::recycleFence(VkFence f) { if (f) m_freeFences.push_back(f); }

/* ═════════════════════════════ shader helpers ════════════════════════════ */
std::vector<char> ResourceManager::readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Cannot open file: " + path);
    size_t sz = static_cast<size_t>(file.tellg());
    std::vector<char> buf(sz);
    file.seekg(0);
    file.read(buf.data(), sz);
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

/* ═════════════════════ generic buffer creation helper ════════════════════ */
void ResourceManager::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem)
{
    VkBufferCreateInfo bc{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bc.size = size;
    bc.usage = usage;
    bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_context->getDevice(), &bc, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error("Buffer create failed");

    VkMemoryRequirements rq{};
    vkGetBufferMemoryRequirements(m_context->getDevice(), buf, &rq);

    VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = rq.size;
    ai.memoryTypeIndex = findMemoryType(rq.memoryTypeBits, props);
    if (vkAllocateMemory(m_context->getDevice(), &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("Buffer memory alloc failed");

    vkBindBufferMemory(m_context->getDevice(), buf, mem, 0);
    g_totalGPUBufferBytes.fetch_add(rq.size, std::memory_order_relaxed);
}

uint32_t ResourceManager::findMemoryType(uint32_t bits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("ResourceManager: suitable memory type not found");
}

/* ════════════════════ public upload API (async) ══════════════════════════ */
void ResourceManager::createChunkBuffers(const std::vector<Vertex>& v,
    const std::vector<uint32_t>& i,
    VkBuffer& vb, VkDeviceMemory& vbMem,
    VkBuffer& ib, VkDeviceMemory& ibMem)
{
    createChunkBuffersAsync(v, i, vb, vbMem, ib, ibMem, nullptr);
}

void ResourceManager::createChunkBuffersAsync(const std::vector<Vertex>& v,
    const std::vector<uint32_t>& i,
    VkBuffer& vb, VkDeviceMemory& vbMem,
    VkBuffer& ib, VkDeviceMemory& ibMem,
    std::function<void()> onComplete)
{
    VkDeviceSize vbSz = sizeof(Vertex) * v.size();
    VkDeviceSize ibSz = sizeof(uint32_t) * i.size();

    createBuffer(vbSz, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vb, vbMem);
    createBuffer(ibSz, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ib, ibMem);

    /* stage data */
    VkDeviceSize total = vbSz + ibSz;
    auto [stBuf, stMem] = getOrCreateStagingBuffer(total);

    if (vbSz)
    {
        void* p; vkMapMemory(m_context->getDevice(), stMem, 0, vbSz, 0, &p);
        std::memcpy(p, v.data(), size_t(vbSz));
        vkUnmapMemory(m_context->getDevice(), stMem);
    }
    if (ibSz)
    {
        void* p; vkMapMemory(m_context->getDevice(), stMem, vbSz, ibSz, 0, &p);
        std::memcpy(p, i.data(), size_t(ibSz));
        vkUnmapMemory(m_context->getDevice(), stMem);
    }

    int copies = int((vbSz ? 1 : 0) + (ibSz ? 1 : 0));
    if (!copies) { if (onComplete) onComplete(); return; }

    auto counter = std::make_shared<std::atomic<int>>(0);
    auto tick = [counter, copies, onComplete]()
        {
            if (++(*counter) == copies && onComplete) onComplete();
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

/* ═════════════════════ destroy / copy helpers ════════════════════════════ */
void ResourceManager::destroyChunkBuffers(VkBuffer vb, VkDeviceMemory vbMem,
    VkBuffer ib, VkDeviceMemory ibMem)
{
    auto destroy = [&](VkBuffer b)
        {
            if (!b) return;
            VkMemoryRequirements rq{};
            vkGetBufferMemoryRequirements(m_context->getDevice(), b, &rq);
            g_totalGPUBufferBytes.fetch_sub(rq.size, std::memory_order_relaxed);
            vkDestroyBuffer(m_context->getDevice(), b, nullptr);
        };
    destroy(vb); destroy(ib);
    if (vbMem) vkFreeMemory(m_context->getDevice(), vbMem, nullptr);
    if (ibMem) vkFreeMemory(m_context->getDevice(), ibMem, nullptr);
}

void ResourceManager::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    if (!size) return;
    VkCommandBuffer cmd = acquireCmd();
    VkFence         fnc = acquireFence();

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy r{ 0, 0, size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &r);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, fnc);
    vkWaitForFences(m_context->getDevice(), 1, &fnc, VK_TRUE, UINT64_MAX);

    recycleFence(fnc); recycleCmd(cmd);
}

void ResourceManager::copyBufferRegions(VkBuffer src, VkBuffer dst,
    const VkBufferCopy* regions, uint32_t count)
{
    if (!regions || !count) return;
    VkCommandBuffer cmd = acquireCmd();
    VkFence         fnc = acquireFence();

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    vkCmdCopyBuffer(cmd, src, dst, count, regions);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, fnc);
    vkWaitForFences(m_context->getDevice(), 1, &fnc, VK_TRUE, UINT64_MAX);

    recycleFence(fnc); recycleCmd(cmd);
}

void ResourceManager::copyBufferRegionsAsync(VkBuffer src, VkBuffer dst,
    const VkBufferCopy* regions, uint32_t count, std::function<void()> cb)
{
    if (!regions || count == 0)
    {
        if (cb) cb();
        return;
    }
    VkCommandBuffer cmd = acquireCmd();
    VkFence         fnc = acquireFence();

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    vkCmdCopyBuffer(cmd, src, dst, count, regions);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, fnc);

    std::lock_guard<std::mutex> lk(g_pendingMutex);
    g_pending.push({ cmd, fnc, std::move(cb) });
}

void ResourceManager::copyBufferAsync(VkBuffer src, VkBuffer dst, VkDeviceSize sz,
    std::function<void()> cb)
{
    if (!sz) { if (cb) cb(); return; }
    VkCommandBuffer cmd = acquireCmd();
    VkFence         fnc = acquireFence();

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy r{ 0, 0, sz };
    vkCmdCopyBuffer(cmd, src, dst, 1, &r);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si, fnc);

    std::lock_guard<std::mutex> lk(g_pendingMutex);
    g_pending.push({ cmd, fnc, std::move(cb) });
}

/* ═════════════════════         stats & flush        ═════════════════════ */
size_t ResourceManager::GetTotalGPUBufferBytes() const
{
    return g_totalGPUBufferBytes.load(std::memory_order_relaxed);
}

void ResourceManager::flushUploads(bool block)
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
    if (g_pending.empty())
        m_currentSlot = (m_currentSlot + 1) % kStagingSlots;
}

void ResourceManager::trimStagingBuffer()
{
    std::lock_guard<std::mutex> g(g_pendingMutex);
    if (!g_pending.empty()) return;
    for (int i = 0; i < kStagingSlots; ++i)
        if (m_slots[i].size > DEFAULT_STAGING_SIZE)
            ensureSlotCapacity(i, DEFAULT_STAGING_SIZE);
}
