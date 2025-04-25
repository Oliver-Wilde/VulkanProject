#pragma once
// ───────────────────────────────────────────────────────────────────────────
// ResourceManager.h   (2025-04-25)
//   • 3-slot staging-buffer ring
//   • All uploads asynchronous
// ───────────────────────────────────────────────────────────────────────────
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <deque>
#include <functional>

class VulkanContext;
struct Vertex;

class ResourceManager
{
public:
    explicit ResourceManager(VulkanContext* ctx);
    ~ResourceManager();

    // ── shaders ───────────────────────────────────────────────────────────
    VkShaderModule loadShaderModule(const std::string& spirvPath);

    // ── chunk buffers (always async now) ──────────────────────────────────
    void createChunkBuffers(const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds,
        VkBuffer& outVB, VkDeviceMemory& outVBmem,
        VkBuffer& outIB, VkDeviceMemory& outIBmem);

    void createChunkBuffersAsync(const std::vector<Vertex>& verts,
        const std::vector<uint32_t>& inds,
        VkBuffer& outVB, VkDeviceMemory& outVBmem,
        VkBuffer& outIB, VkDeviceMemory& outIBmem,
        std::function<void()> onComplete = {});

    void destroyChunkBuffers(VkBuffer vb, VkDeviceMemory vbMem,
        VkBuffer ib, VkDeviceMemory ibMem);

    size_t GetTotalGPUBufferBytes() const;

    void flushUploads(bool block = false);     // call once per-frame
    void trimStagingBuffer();                  // optional memory tidy-up

    // ── raw copy helpers (prefer async) ───────────────────────────────────
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    void copyBufferRegions(VkBuffer src, VkBuffer dst,
        const VkBufferCopy* regions, uint32_t regionCount);

    void copyBufferAsync(VkBuffer src, VkBuffer dst, VkDeviceSize size,
        std::function<void()> onComplete = {});

    void copyBufferRegionsAsync(VkBuffer src, VkBuffer dst,
        const VkBufferCopy* regions, uint32_t regionCount,
        std::function<void()> onComplete = {});

private:
    // ── staging-ring helpers ──────────────────────────────────────────────
    struct StagingSlot
    {
        VkBuffer       buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize   size = 0;
    };
    static constexpr int kStagingSlots = 3;
    StagingSlot   m_slots[kStagingSlots];
    uint32_t      m_currentSlot = 0;

    void   ensureSlotCapacity(int slot, VkDeviceSize wantSize);
    StagingSlot& currentSlot();
    std::pair<VkBuffer, VkDeviceMemory>
        getOrCreateStagingBuffer(VkDeviceSize size);

    // ── allocator & misc helpers ──────────────────────────────────────────
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags props,
        VkBuffer& buf, VkDeviceMemory& mem);

    uint32_t findMemoryType(uint32_t bits, VkMemoryPropertyFlags props);
    std::vector<char> readFile(const std::string& path);

    // ── command-buffer / fence cache ─────────────────────────────────────
    VkCommandBuffer acquireCmd();
    void            recycleCmd(VkCommandBuffer cmd);
    VkFence         acquireFence();
    void            recycleFence(VkFence f);

private:
    VulkanContext* m_context = nullptr;

    // pools & caches
    VkCommandPool               m_transferPool = VK_NULL_HANDLE;
    std::deque<VkCommandBuffer> m_freeCmdBuffers;
    std::deque<VkFence>         m_freeFences;

    std::unordered_map<std::string, VkShaderModule> m_shaderModules;
};
