#pragma once
// ───────────────────────────────────────────────────────────────────────────
// ResourceManager.h   – GPU buffer + shader management with async uploads
//   • 2025-04-24: adds per-frame command-buffer / fence cache to avoid
//                driver-level allocations on every async transfer.
// ───────────────────────────────────────────────────────────────────────────
#include <vulkan/vulkan.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <deque>          // free-lists

class VulkanContext;
struct Vertex;

/*=============================================================================
  ResourceManager
    - owns staging buffers, manages vkBuffer/vkMemory creation,
      tracks total GPU bytes, loads SPIR-V modules, supports asynchronous
      geometry uploads, and now reuses command buffers & fences.
=============================================================================*/
class ResourceManager
{
public:
    explicit ResourceManager(VulkanContext* ctx);
    ~ResourceManager();

    // ── Shader modules ────────────────────────────────────────────────────
    VkShaderModule loadShaderModule(const std::string& spirvPath);

    // ── Chunk vertex / index buffers  (synchronous & asynchronous) ───────
    /** Traditional blocking upload: returns only when data is on the GPU. */
    void createChunkBuffers(const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds,
        VkBuffer& outVB, VkDeviceMemory& outVBmem,
        VkBuffer& outIB, VkDeviceMemory& outIBmem);

    /** Non-blocking variant: schedules an async transfer. */
    void createChunkBuffersAsync(const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds,
        VkBuffer& outVB, VkDeviceMemory& outVBmem,
        VkBuffer& outIB, VkDeviceMemory& outIBmem,
        std::function<void()> onComplete = {});

    /** Frees both VB/IB and their device memory, tracking global byte usage. */
    void destroyChunkBuffers(VkBuffer vb, VkDeviceMemory vbMem,
        VkBuffer ib, VkDeviceMemory ibMem);

    /** Debug: total bytes of GPU memory currently allocated for buffers. */
    size_t GetTotalGPUBufferBytes() const;

    /** Flush the async-upload queue; call once per-frame. */
    void flushUploads(bool block = false);

    /** Shrink an oversized staging buffer after bootstrap.                */
    void trimStagingBuffer();

    // ── Raw copy helpers (sync & async) ───────────────────────────────────
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    void copyBufferRegions(VkBuffer src, VkBuffer dst,
        const VkBufferCopy* regions, uint32_t regionCount);

    void copyBufferAsync(VkBuffer src, VkBuffer dst, VkDeviceSize size,
        std::function<void()> onComplete = {});

    void copyBufferRegionsAsync(VkBuffer src, VkBuffer dst,
        const VkBufferCopy* regions, uint32_t regionCount,
        std::function<void()> onComplete = {});

private:
    // ── internal helpers ─────────────────────────────────────────────────
    std::vector<char> readFile(const std::string& path);

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags props,
        VkBuffer& buf, VkDeviceMemory& mem);

    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

    void createStagingBuffer(VkDeviceSize size);
    std::pair<VkBuffer, VkDeviceMemory> getOrCreateStagingBuffer(VkDeviceSize size);

    /* NEW: command-buffer / fence cache used by async transfers ----------- */
    VkCommandBuffer acquireCmd();
    void            recycleCmd(VkCommandBuffer cmd);

    VkFence         acquireFence();
    void            recycleFence(VkFence fence);

private:
    VulkanContext* m_context = nullptr;

    std::unordered_map<std::string, VkShaderModule> m_shaderModules;

    // ── staging buffer (host-visible) ────────────────────────────────────
    VkBuffer       m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize   m_stagingBufferSize = 0;

    // ── transfer pool & reusable objects (NEW) ───────────────────────────
    VkCommandPool               m_transferPool = VK_NULL_HANDLE;
    std::deque<VkCommandBuffer> m_freeCmdBuffers;
    std::deque<VkFence>         m_freeFences;
};
