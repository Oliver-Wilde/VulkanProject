#pragma once
// ───────────────────────────────────────────────────────────────────────────
// ResourceManager.h   (2025-04-29)
//   • Per-slot busy flags added for safe staging-ring reuse
//   • m_currentSlot now selected via first-free search
//   • BENCHMARK_MODE: per-frame upload counters & budget accessors
// ───────────────────────────────────────────────────────────────────────────
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <deque>
#include <functional>
#include <atomic>

class VulkanContext;
struct Vertex;

namespace gfx { class IndirectBatch; }

class ResourceManager
{
    friend class gfx::IndirectBatch;

public:
    explicit ResourceManager(VulkanContext* ctx);
    ~ResourceManager();

    // ── shaders ───────────────────────────────────────────────────────────
    VkShaderModule loadShaderModule(const std::string& spirvPath);

    // ── chunk buffers (always async) ──────────────────────────────────────
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

    void flushUploads(bool block = false);   // call once per frame
    void trimStagingBuffer();                // optional tidy-up

#ifdef BENCHMARK_MODE
    /* ─────────── per-frame upload telemetry ─────────── */
    size_t getBytesUploadedThisFrame() const { return m_bytesUploadedThisFrame.load(); }
    size_t getUploadBudgetThisFrame()  const { return currentSlot().size; }
    void   resetFrameStats() { m_bytesUploadedThisFrame.store(0); }
#endif

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

    StagingSlot           m_slots[kStagingSlots];
    std::atomic_bool      m_slotBusy[kStagingSlots]{};
    uint32_t              m_currentSlot = 0;

    void ensureSlotCapacity(int slot, VkDeviceSize wantSize);

    /* non-const and const overloads so callers can use either */
    StagingSlot& currentSlot();
    const StagingSlot& currentSlot() const { return m_slots[m_currentSlot]; }

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

#ifdef BENCHMARK_MODE
    /* accumulated bytes uploaded during the current frame */
    std::atomic<size_t> m_bytesUploadedThisFrame{ 0 };
#endif
};
