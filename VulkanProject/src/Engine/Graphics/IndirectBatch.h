// Engine/Graphics/IndirectBatch.h
// -----------------------------------------------------------------------------
// Builds one device‑local VkDrawIndexedIndirect buffer each frame.  Draws
// reference an external packed VBO, so commands need {indexCount, firstIndex,
// baseVertex}.  Only index data are copied here (optional legacy path).
// -----------------------------------------------------------------------------
#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include "Engine/Graphics/VulkanContext.h"

class ResourceManager;        // engine‑level (global namespace)

namespace gfx
{
    /**
     * Usage each frame:
     *   beginFrame();
     *   addDraw(indexCount, firstIndex, baseVertex) …
     *   endFrame(queue);
     *   vkCmdBindIndexBuffer(cmd, getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
     *   vkCmdDrawIndexedIndirect(cmd, getIndirectBuffer(), 0, drawCount,
     *                             sizeof(VkDrawIndexedIndirectCommand));
     */
    class IndirectBatch
    {
    public:
        IndirectBatch() = default;
        ~IndirectBatch();
        IndirectBatch(const IndirectBatch&) = delete;
        IndirectBatch& operator=(const IndirectBatch&) = delete;

        void init(VulkanContext* ctx, ResourceManager* rm,
            VkDeviceSize maxIBBytes = 8 * 1024 * 1024);
        void cleanup();

        void beginFrame();
        void addDraw(uint32_t indexCount,
            uint32_t firstIndex,
            uint32_t baseVertex);
        VkFence endFrame(VkQueue graphicsQueue);

        // Accessors ------------------------------------------------------
        VkBuffer getIndexBuffer()   const { return m_indexBuffer; }
        VkBuffer getIndirectBuffer()const { return m_drawCmdBuffer; }
        uint32_t getDrawCount()     const { return m_drawCount; }

    private:
        struct StagingBuf { VkBuffer buffer = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; VkDeviceSize capacity = 0; };

        VulkanContext* m_ctx = nullptr;
        ResourceManager* m_rm = nullptr;

        StagingBuf       m_stagingIB{};       // host‑visible (legacy)
        VkBuffer         m_indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory   m_indexMemory = VK_NULL_HANDLE;
        VkBuffer         m_drawCmdBuffer = VK_NULL_HANDLE;
        VkDeviceMemory   m_drawCmdMemory = VK_NULL_HANDLE;

        VkDeviceSize     m_indexCapacity = 0;
        VkDeviceSize     m_cmdCapacity = 0;

        // per‑frame
        uint32_t         m_drawCount = 0;
        VkDeviceSize     m_indexBytes = 0;

        uint8_t* m_mappedIB = nullptr;
        VkDrawIndexedIndirectCommand* m_mappedCmd = nullptr;

        void ensureIndexCapacity(VkDeviceSize wantBytes);
        void ensureCmdCapacity(uint32_t wantDraws);
        static VkDeviceSize align256(VkDeviceSize v) { return (v + 255) & ~VkDeviceSize(255); }
    };
} // namespace gfx
