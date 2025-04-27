// Engine/Graphics/IndirectBatch.h
// -----------------------------------------------------------------------------
// Helper for building a single VkDrawIndexedIndirect buffer that batches many
// chunk draw calls into one.  The buffer is rebuilt every frame from the list
// of visible chunks.  Thread‑safe via explicit begin()/add()/end() staging.
// -----------------------------------------------------------------------------
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include "Engine/Graphics/VulkanContext.h"

// Forward declarations
class ResourceManager;
class Chunk;

namespace gfx
{

    /**
     * @brief A transient builder that assembles a VK_BUFFER_USAGE_INDIRECT_BUFFER
     *        with VkDrawIndexedIndirectCommand entries followed by a matching
     *        tightly‑packed index buffer.  Vertex data are bound once per draw
     *        via baseVertex.
     *
     * Usage pattern per frame:
     *   1. beginFrame(ctx, rm, commandPool)
     *   2. for (Chunk *c : visible) addChunk(c);
     *   3. endFrame(queue) – submits copy to device‑local IB + CMD upload fence
     *   4. getIndirectBuffer() when binding before vkCmdDrawIndexedIndirect.
     */
    class IndirectBatch
    {
    public:
        IndirectBatch() = default;
        ~IndirectBatch();

        IndirectBatch(const IndirectBatch&) = delete;
        IndirectBatch& operator=(const IndirectBatch&) = delete;

        /** Initialise persistent GPU resources. Call once on startup. */
        void init(VulkanContext* ctx, ResourceManager* rm, VkDeviceSize maxIBBytes = 8 * 1024 * 1024);

        /** Destroy GPU resources. */
        void cleanup();

        /** Start a new frame – resets counters & stages host‑visible upload. */
        void beginFrame();

        /** Append a chunk. Thread‑unsafe, call from render thread only. */
        void addChunk(const Chunk* chunk);

        /**
         * Finalise: copy host data into device‑local buffers and build the indirect
         * command buffer.  Non‑blocking variant – returns a fence you may wait on.
         */
        VkFence endFrame(VkQueue graphicsQueue);

        /** GPU‑local indirect‑draw buffer handle. Valid after endFrame fence. */
        VkBuffer getIndirectBuffer() const { return m_drawCmdBuffer; }
        uint32_t getDrawCount()     const { return m_drawCount; }

    private:
        struct Staging
        {
            VkBuffer       buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDeviceSize   capacity = 0;         // bytes
        } m_stagingIB{};                         // host‑visible index staging

        VulkanContext* m_ctx = nullptr;
        ResourceManager* m_rm = nullptr;

        // Device‑local resources reused every frame
        VkBuffer        m_indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory  m_indexMemory = VK_NULL_HANDLE;
        VkBuffer        m_drawCmdBuffer = VK_NULL_HANDLE;
        VkDeviceMemory  m_drawCmdMemory = VK_NULL_HANDLE;
        VkDeviceSize    m_indexCapacity = 0;
        VkDeviceSize    m_cmdCapacity = 0;

        // Per‑frame counters
        uint32_t        m_drawCount = 0;
        VkDeviceSize    m_indexBytes = 0;

        // Host‑side write pointer into staging IB
        uint8_t* m_mappedIB = nullptr;
        VkDrawIndexedIndirectCommand* m_mappedCmd = nullptr;

        void ensureIndexCapacity(VkDeviceSize wantBytes);
        void ensureCmdCapacity(uint32_t wantDraws);
    };

} // namespace gfx
