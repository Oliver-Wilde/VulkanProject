// Engine/Graphics/IndirectBatch.cpp
// -----------------------------------------------------------------------------
// 2025-04-29  – Refactored for packed-VBO workflow
// -----------------------------------------------------------------------------
#include "IndirectBatch.h"
#include "Engine/Resources/ResourceManager.h"
#include <stdexcept>                 // <— fix: std::runtime_error

using namespace gfx;

namespace {
    static VkDeviceSize align256(VkDeviceSize v) { return (v + 255) & ~VkDeviceSize(255); }
}

// ─────────────────────────────────────────────────────────────────────────────
IndirectBatch::~IndirectBatch() { cleanup(); }

// -----------------------------------------------------------------------------
// init – allocate persistent GPU buffers (index optional, cmd mandatory)
// -----------------------------------------------------------------------------
void IndirectBatch::init(VulkanContext* ctx,
    ResourceManager* rm,
    VkDeviceSize     firstIBCap)
{
    if (!ctx || !rm) throw std::runtime_error("IndirectBatch::init – invalid args");
    m_ctx = ctx; m_rm = rm;

    /* optional index buffer (legacy path) */
    ensureIndexCapacity(firstIBCap);
    /* command buffer – host-visible */
    ensureCmdCapacity(1024);
}

// -----------------------------------------------------------------------------
// cleanup
// -----------------------------------------------------------------------------
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

    m_indexCapacity = m_cmdCapacity = 0;
    m_ctx = nullptr; m_rm = nullptr;
}

// -----------------------------------------------------------------------------
// beginFrame – reset counters & map host memories
// -----------------------------------------------------------------------------
void IndirectBatch::beginFrame()
{
    m_drawCount = 0;
    m_indexBytes = 0;

    if (m_drawCmdBuffer && !m_mappedCmd)
        vkMapMemory(m_ctx->getDevice(), m_drawCmdMemory, 0, m_cmdCapacity, 0,
            reinterpret_cast<void**>(&m_mappedCmd));

    if (m_stagingIB.buffer && !m_mappedIB)
        vkMapMemory(m_ctx->getDevice(), m_stagingIB.memory, 0, m_stagingIB.capacity, 0,
            reinterpret_cast<void**>(&m_mappedIB));
}

// -----------------------------------------------------------------------------
// addDraw – append one indirect command
// -----------------------------------------------------------------------------
void IndirectBatch::addDraw(uint32_t indexCount,
    uint32_t firstIndex,
    uint32_t baseVertex)
{
    ensureCmdCapacity(m_drawCount + 1);

    VkDrawIndexedIndirectCommand cmd{};
    cmd.indexCount = indexCount;
    cmd.instanceCount = 1;
    cmd.firstIndex = firstIndex;
    cmd.vertexOffset = static_cast<int32_t>(baseVertex);
    cmd.firstInstance = 0;

    m_mappedCmd[m_drawCount++] = cmd;
}

// -----------------------------------------------------------------------------
// endFrame – flush/unmap cmd buffer; returns VK_NULL_HANDLE (host-coherent)
// -----------------------------------------------------------------------------
VkFence IndirectBatch::endFrame(VkQueue /*graphicsQueue*/)
{
    if (m_mappedCmd)
    {
        vkUnmapMemory(m_ctx->getDevice(), m_drawCmdMemory);
        m_mappedCmd = nullptr;
    }
    if (m_mappedIB)
    {
        vkUnmapMemory(m_ctx->getDevice(), m_stagingIB.memory);
        m_mappedIB = nullptr;
    }
    return VK_NULL_HANDLE;
}

// -----------------------------------------------------------------------------
// ensureIndexCapacity / ensureCmdCapacity (unchanged except namespace qual)
// -----------------------------------------------------------------------------
void IndirectBatch::ensureIndexCapacity(VkDeviceSize want)
{
    if (want <= m_stagingIB.capacity) return;
    VkDeviceSize newCap = align256(std::max(want, m_stagingIB.capacity * 3 / 2 + 65536));

    VkDevice dev = m_ctx->getDevice();

    auto destroy = [&](VkBuffer& b, VkDeviceMemory& m)
        {
            if (b) vkDestroyBuffer(dev, b, nullptr);
            if (m) vkFreeMemory(dev, m, nullptr);
            b = VK_NULL_HANDLE; m = VK_NULL_HANDLE;
        };
    destroy(m_stagingIB.buffer, m_stagingIB.memory);
    destroy(m_indexBuffer, m_indexMemory);

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

    m_rm->createBuffer(newCap,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_drawCmdBuffer, m_drawCmdMemory);

    m_cmdCapacity = newCap;
}
