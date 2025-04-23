#pragma once
// ───────────────────────────────────────────────────────────────────────────
// ResourceManager.h   – GPU buffer + shader management with async uploads
// ───────────────────────────────────────────────────────────────────────────
#include <vulkan/vulkan.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>   // std::function

class VulkanContext;
struct Vertex;

/*=============================================================================
  ResourceManager
    - owns staging buffers, manages vkBuffer/vkMemory creation,
      tracks total GPU bytes, loads SPIR-V modules, and now supports
      asynchronous chunk-geometry uploads.
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

    /** Debug info: total bytes of GPU memory currently allocated for buffers. */
    size_t GetTotalGPUBufferBytes() const;

    /** Flush the async-upload queue; call once per-frame
        (pass block = true only at shutdown). */
    void flushUploads(bool block = false);

    /** NEW: Optionally free or shrink an oversized staging buffer once the
        big terrain bootstrap is finished.  Safe to call every frame.       */
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

private:
    VulkanContext* m_context = nullptr;

    std::unordered_map<std::string, VkShaderModule> m_shaderModules;

    // Re-usable host-visible staging buffer for geometry uploads
    VkBuffer       m_stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize   m_stagingBufferSize = 0;
};
