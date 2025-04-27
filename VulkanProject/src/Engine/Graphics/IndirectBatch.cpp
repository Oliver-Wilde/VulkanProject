// Engine/Graphics/IndirectBatch.cpp
// -----------------------------------------------------------------------------
// Revised 2025-04-27: remove use of deleted assignment operator and redundant
// command-buffer copy; the indirect-draw command buffer stays host-visible so
// we simply write it every frame and unmap.
// -----------------------------------------------------------------------------
#include "IndirectBatch.h"
#include "Engine/Resources/ResourceManager.h"
#include "Engine/Voxels/Chunk.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

using namespace gfx;

namespace
{
    static VkDeviceSize align256(VkDeviceSize v) { return (v + 255) & ~VkDeviceSize(255); }
}

// ─────────────────────────────────────────────────────────────────────────────
IndirectBatch::~IndirectBatch()
{
    cleanup();
}

// -----------------------------------------------------------------------------
void IndirectBatch::init(VulkanContext* ctx, ResourceManager* rm, VkDeviceSize firstIBCap)
{
    if (!ctx || !rm) throw std::runtime_error("IndirectBatch::init – invalid args");
    m_ctx = ctx; m_rm = rm;

    ensureIndexCapacity(firstIBCap);
    ensureCmdCapacity(1024); // space for 1 k draws initially
}

void IndirectBatch::cleanup()
{
    if (!m_ctx) return;
    VkDevice dev = m_ctx->getDevice();

    auto destroy = [&](VkBuffer& b, VkDeviceMemory& m)
        {
            if (b) vkDestroyBuffer(dev, b, nullptr);
            if (m) vkFreeMemory(dev, m, nullptr);
            b = VK_NULL_HANDLE; m = VK_NULL_HANDLE;
        };

    destroy(m_stagingIB.buffer, m_stagingIB.memory);
    destroy(m_indexBuffer, m_indexMemory);
    destroy(m_drawCmdBuffer, m_drawCmdMemory);

    m_stagingIB.capacity = m_indexCapacity = m_cmdCapacity = 0;
    m_ctx = nullptr; m_rm = nullptr;
}

// -----------------------------------------------------------------------------
void IndirectBatch::beginFrame()
{
    m_drawCount = 0;
    m_indexBytes = 0;

    // Map host-visible memories once per frame
    if (m_stagingIB.buffer && !m_mappedIB)
    {
        vkMapMemory(m_ctx->getDevice(), m_stagingIB.memory, 0, m_stagingIB.capacity, 0,
            reinterpret_cast<void**>(&m_mappedIB));
    }
    if (m_drawCmdBuffer && !m_mappedCmd)
    {
        vkMapMemory(m_ctx->getDevice(), m_drawCmdMemory, 0, m_cmdCapacity, 0,
            reinterpret_cast<void**>(&m_mappedCmd));
    }
}

// -----------------------------------------------------------------------------
void IndirectBatch::addChunk(const Chunk* chunk)
{
    if (!chunk) return;
    VkBuffer srcIB = chunk->getIndexBuffer();
    uint32_t idxCount = chunk->getIndexCount();

    if (srcIB == VK_NULL_HANDLE || idxCount == 0) return;

    VkDeviceSize bytes = idxCount * sizeof(uint32_t);
    ensureIndexCapacity(m_indexBytes + bytes);
    ensureCmdCapacity(m_drawCount + 1);

    // Copy index data into staging (blocking host memcpy)
    const void* srcPtr = nullptr;
    vkMapMemory(m_ctx->getDevice(), chunk->getIndexMemory(), 0, bytes, 0,
        const_cast<void**>(&srcPtr));
    std::memcpy(m_mappedIB + m_indexBytes, srcPtr, static_cast<size_t>(bytes));
    vkUnmapMemory(m_ctx->getDevice(), chunk->getIndexMemory());

    // Write command entry
    VkDrawIndexedIndirectCommand cmd{};
    cmd.indexCount = idxCount;
    cmd.instanceCount = 1;
    cmd.firstIndex = static_cast<uint32_t>(m_indexBytes / sizeof(uint32_t));
    cmd.vertexOffset = 0;          // vertices still bound per-chunk externally
    cmd.firstInstance = 0;

    m_mappedCmd[m_drawCount++] = cmd;
    m_indexBytes += bytes;
}

// -----------------------------------------------------------------------------
VkFence IndirectBatch::endFrame(VkQueue queue)
{
    // Flush & unmap staging memories so GPU sees data
    if (m_mappedIB)
    {
        vkUnmapMemory(m_ctx->getDevice(), m_stagingIB.memory);
        m_mappedIB = nullptr;
    }
    if (m_mappedCmd)
    {
        vkUnmapMemory(m_ctx->getDevice(), m_drawCmdMemory);
        m_mappedCmd = nullptr;
    }

    if (m_drawCount == 0) return VK_NULL_HANDLE;

    // Build one-time copy of indices into device-local buffer
    VkCommandBuffer cmd = m_rm->acquireCmd();
    VkFence         fnc = m_rm->acquireFence();

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkBufferCopy region{ 0, 0, align256(m_indexBytes) };
    vkCmdCopyBuffer(cmd, m_stagingIB.buffer, m_indexBuffer, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, fnc);

    return fnc; // caller (ResourceManager/Renderer) decides when to wait
}

// -----------------------------------------------------------------------------
void IndirectBatch::ensureIndexCapacity(VkDeviceSize want)
{
    if (want <= m_stagingIB.capacity) return;
    VkDeviceSize newCap = align256(std::max(want, m_stagingIB.capacity * 3 / 2 + 65536));

    VkDevice dev = m_ctx->getDevice();

    // destroy old staging / gpu buffer
    if (m_stagingIB.buffer) {
        vkDestroyBuffer(dev, m_stagingIB.buffer, nullptr);
        vkFreeMemory(dev, m_stagingIB.memory, nullptr);
    }
    if (m_indexBuffer) {
        vkDestroyBuffer(dev, m_indexBuffer, nullptr);
        vkFreeMemory(dev, m_indexMemory, nullptr);
    }

    // recreate staging (host visible) & device-local buffers
    m_rm->createBuffer(newCap,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_stagingIB.buffer, m_stagingIB.memory);

    m_rm->createBuffer(newCap,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_indexBuffer, m_indexMemory);

    m_stagingIB.capacity = m_indexCapacity = newCap;
}

void IndirectBatch::ensureCmdCapacity(uint32_t wantDraws)
{
    VkDeviceSize wantBytes = wantDraws * sizeof(VkDrawIndexedIndirectCommand);
    if (wantBytes <= m_cmdCapacity) return;

    VkDeviceSize newCap = align256(std::max(wantBytes, m_cmdCapacity * 3 / 2 + 1024));

    VkDevice dev = m_ctx->getDevice();
    if (m_drawCmdBuffer)
    {
        vkDestroyBuffer(dev, m_drawCmdBuffer, nullptr);
        vkFreeMemory(dev, m_drawCmdMemory, nullptr);
    }

    // host-visible so we can write commands directly
    m_rm->createBuffer(newCap,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_drawCmdBuffer, m_drawCmdMemory);

    m_cmdCapacity = newCap;
}
