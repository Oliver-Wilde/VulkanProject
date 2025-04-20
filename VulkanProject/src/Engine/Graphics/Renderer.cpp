#include "Renderer.h"

#include "Engine/Core/Window.h"
#include "Engine/Core/Time.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Graphics/SwapChain.h"
#include "Engine/Graphics/RenderPassManager.h"
#include "Engine/Graphics/PipelineManager.h"
#include "Engine/Resources/ResourceManager.h"
#include "Engine/Graphics/Frustum.h"

#include "UIRenderer.h"

#include "Engine/Voxels/VoxelWorld.h"
#include "Engine/Voxels/Chunk.h"
#include "Engine/Scene/Camera.h"

#include "Frustum.h"
#include "../Utils/CpuProfiler.h"
#include <Engine/Utils/Logger.h>
#include <Engine/Utils/ThreadPool.h>

#include <stdexcept>
#include <deque>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

extern ThreadPool g_threadPool;
static CpuProfiler g_cpuProfiler;              // global CPU profiler
static uint64_t    s_frameCounter = 0;         // running frame counter

// ============================================================================
// Constructor / Destructor
// ============================================================================
Renderer::Renderer(VulkanContext* context, Window* window, VoxelWorld* voxelWorld)
    : m_context(context)
    , m_window(window)
    , m_voxelWorld(voxelWorld)
{
    // 1) Swap chain
    m_swapChain = new SwapChain();
    m_swapChain->init(m_context, m_window);

    // 2) Managers
    m_resourceMgr = new ResourceManager(m_context);
    m_pipelineMgr = new PipelineManager(m_context, m_resourceMgr);
    m_rpManager = new RenderPassManager(m_context, m_swapChain);

    // 3) Render pass + framebuffers
    m_rpManager->createRenderPass();
    m_rpManager->createFramebuffers();

    // 4) Pipelines (fill & wireframe) + descriptor layout for MVP
    auto extent = m_swapChain->getExtent();
    auto renderPass = m_rpManager->getRenderPass();

    m_mvpLayout = m_pipelineMgr->createMVPDescriptorSetLayout();
    m_pipelineMgr->createVoxelPipelineFill("voxel_fill", renderPass, extent, m_mvpLayout);
    m_pipelineMgr->createVoxelPipelineWireframe("voxel_wireframe", renderPass, extent, m_mvpLayout);

    // 5) Uniform buffer for MVP
    createMVPUniformBuffer();

    // 6) Per‑frame resources (command buffers, semaphores, fences)
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.commandPool = m_context->getCommandPool();
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_context->getDevice(), &cmdAlloc,
            &m_frames[i].commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Renderer: failed to allocate command buffer");
        }

        VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

        if (vkCreateSemaphore(m_context->getDevice(), &semInfo, nullptr,
            &m_frames[i].imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(m_context->getDevice(), &semInfo, nullptr,
                &m_frames[i].renderFinishedSemaphore) != VK_SUCCESS)
        {
            throw std::runtime_error("Renderer: failed to create semaphores");
        }

        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr,
            &m_frames[i].inFlightFence) != VK_SUCCESS)
        {
            throw std::runtime_error("Renderer: failed to create fences");
        }
    }

    // 7) UIRenderer (ImGui)
    m_uiRenderer = new UIRenderer();
    m_uiRenderer->init(
        m_context,
        m_window,
        m_rpManager->getRenderPass(),
        m_swapChain->getImageCount());
}

Renderer::~Renderer()
{
    vkDeviceWaitIdle(m_context->getDevice());

    // -- per‑frame objects --
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (m_frames[i].commandBuffer)
            vkFreeCommandBuffers(m_context->getDevice(),
                m_context->getCommandPool(),
                1, &m_frames[i].commandBuffer);

        if (m_frames[i].imageAvailableSemaphore)
            vkDestroySemaphore(m_context->getDevice(),
                m_frames[i].imageAvailableSemaphore,
                nullptr);
        if (m_frames[i].renderFinishedSemaphore)
            vkDestroySemaphore(m_context->getDevice(),
                m_frames[i].renderFinishedSemaphore,
                nullptr);
        if (m_frames[i].inFlightFence)
            vkDestroyFence(m_context->getDevice(),
                m_frames[i].inFlightFence,
                nullptr);
    }

    // -- MVP resources --
    if (m_mvpBuffer)          vkDestroyBuffer(m_context->getDevice(), m_mvpBuffer, nullptr);
    if (m_mvpMemory)          vkFreeMemory(m_context->getDevice(), m_mvpMemory, nullptr);
    if (m_mvpDescriptorPool)  vkDestroyDescriptorPool(m_context->getDevice(), m_mvpDescriptorPool, nullptr);
    if (m_mvpLayout)          vkDestroyDescriptorSetLayout(m_context->getDevice(), m_mvpLayout, nullptr);

    // -- UI --
    if (m_uiRenderer) { m_uiRenderer->cleanup(); delete m_uiRenderer; m_uiRenderer = nullptr; }

    // -- managers --
    delete m_rpManager;   m_rpManager = nullptr;
    delete m_pipelineMgr; m_pipelineMgr = nullptr;
    delete m_resourceMgr; m_resourceMgr = nullptr;

    // -- swap chain --
    if (m_swapChain) { m_swapChain->cleanup(); delete m_swapChain; m_swapChain = nullptr; }
}

// ============================================================================
// Small public helpers
// ============================================================================
void Renderer::setTime(Time* t) { m_time = t; }
void Renderer::setCamera(const Camera& cam) { m_camera = cam; }
void Renderer::toggleWireframe() { m_wireframeOn ^= true; }
void Renderer::enableFrustumCulling(bool enable) { m_enableFrustumCulling = enable; }

// ============================================================================
// Deferred destruction helpers
// ============================================================================
void Renderer::enqueueDeferredDestroy(const QueuedChunkDestruction& q)
{
    m_deferredFrees[m_currentFrame].push_back(q);
}

void Renderer::freeDeferredResources()
{
    auto& lst = m_deferredFrees[m_currentFrame];
    if (lst.empty()) return;

    for (auto& q : lst)
        m_resourceMgr->destroyChunkBuffers(q.vb, q.vbMem, q.ib, q.ibMem);

    lst.clear();
}

// ============================================================================
// Rolling‑average helpers
// ============================================================================
void Renderer::addSample(std::deque<float>& buf, float v)
{
    if (buf.size() >= ROLLING_AVG_SAMPLES) buf.pop_front();
    buf.push_back(v);
}
float Renderer::computeAverage(const std::deque<float>& buf)
{
    if (buf.empty()) return 0.f;
    float s = 0.f;
    for (float v : buf) s += v;
    return s / buf.size();
}

// ============================================================================
// renderFrame  (unchanged logic; only comments trimmed)
// ============================================================================
void Renderer::renderFrame()
{
    CpuProfiler::ScopedTimer frameTimer("Renderer::renderFrame");

    updateMVP();

    /* ---------- wait fence ---------- */
    vkWaitForFences(m_context->getDevice(), 1,
        &m_frames[m_currentFrame].inFlightFence,
        VK_TRUE, UINT64_MAX);
    freeDeferredResources();
    vkResetFences(m_context->getDevice(), 1,
        &m_frames[m_currentFrame].inFlightFence);

    /* ---------- acquire image ---------- */
    uint32_t imageIndex;
    VkResult res = vkAcquireNextImageKHR(
        m_context->getDevice(),
        m_swapChain->getSwapChain(),
        UINT64_MAX,
        m_frames[m_currentFrame].imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex);

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapChain();
        return;
    }
    else if (res != VK_SUCCESS)
    {
        throw std::runtime_error("Renderer: failed to acquire swap image");
    }

    /* ---------- record command buffer ---------- */
    VkCommandBuffer cmd = m_frames[m_currentFrame].commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &begin);

    VkClearValue clear[2];
    clear[0].color = { {0.1f, 0.2f, 0.3f, 1.f} };
    clear[1].depthStencil = { 1.f, 0 };

    VkRenderPassBeginInfo rpBegin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass = m_rpManager->getRenderPass();
    rpBegin.framebuffer = m_rpManager->getFramebuffers()[imageIndex];
    rpBegin.renderArea = { {0,0}, m_swapChain->getExtent() };
    rpBegin.clearValueCount = 2;
    rpBegin.pClearValues = clear;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    /* ---------- bind pipeline ---------- */
    std::string pName = m_wireframeOn ? "voxel_wireframe" : "voxel_fill";
    auto pInfo = m_pipelineMgr->getPipeline(pName);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pInfo.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pInfo.pipelineLayout,
        0, 1, &m_mvpDescriptorSet, 0, nullptr);

    /* ---------- gather stats ---------- */
    float dt = m_time ? m_time->getDeltaTime() : 0.f;
    float fps = dt > 0.f ? 1.f / dt : 0.f;
    addSample(m_fpsSamples, fps);
    float avgFps = computeAverage(m_fpsSamples);

    float cpuUse = g_cpuProfiler.GetCpuUsage();
    addSample(m_cpuSamples, cpuUse);
    float avgCpu = computeAverage(m_cpuSamples);

    /* ---------- optional frustum ---------- */
    Frustum viewFrustum;
    if (m_enableFrustumCulling)
        viewFrustum = buildCameraFrustum(m_camera, m_swapChain->getExtent());

    /* ---------- draw chunks ---------- */
    uint32_t totalVerts = 0;
    uint32_t drawCalls = 0;

    if (m_voxelWorld)
    {
        const auto& chunks = m_voxelWorld->getChunkManager().getAllChunks();
        bool multiLOD = m_voxelWorld->isUsingMultiLOD();

        for (auto& kv : chunks)
        {
            Chunk* c = kv.second.get();
            if (!c) continue;

            if (m_enableFrustumCulling)
            {
                glm::vec3 mn, mx;
                c->getBoundingBox(mn, mx);
                if (!viewFrustum.intersectsAABB(mn, mx))
                    continue;
            }

            if (multiLOD)
            {
                float cx = (c->worldX() + 0.5f) * Chunk::SIZE_X;
                float cy = (c->worldY() + 0.5f) * Chunk::SIZE_Y;
                float cz = (c->worldZ() + 0.5f) * Chunk::SIZE_Z;
                float dist = glm::distance(m_camera.position,
                    glm::vec3(cx, cy, cz));

                int lod = 0;
                if (dist > 250.f) lod = 1;
                if (dist > 500.f) lod = 2;
                if (dist > 750.f) lod = 3;
                if (dist > 6000.f) lod = 4;
                if (dist > 8000.f) lod = 5;
                if (dist > 16000.f) { continue; }

                if (lod >= Chunk::MAX_LOD_LEVELS) lod = Chunk::MAX_LOD_LEVELS - 1;

                const auto& L = c->getLODData(lod);
                if (L.vertexBuffer == VK_NULL_HANDLE || L.indexCount == 0)
                    continue;

                totalVerts += L.vertexCount;

                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &L.vertexBuffer, &off);
                vkCmdBindIndexBuffer(cmd, L.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, L.indexCount, 1, 0, 0, 0);
                drawCalls++;
            }
            else
            {
                if (!c->getVertexBuffer() || !c->getIndexBuffer())
                    continue;

                totalVerts += c->getVertexCount();

                VkDeviceSize off = 0;
                auto vb = c->getVertexBuffer();
                auto ib = c->getIndexBuffer();

                vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &off);
                vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, c->getIndexCount(), 1, 0, 0, 0);
                drawCalls++;
            }
        }
    }

    /* ---------- UI ---------- */
    m_uiRenderer->beginFrame();
    m_uiRenderer->renderDebugWindow(dt, fps, avgFps,
        cpuUse, avgCpu,
        totalVerts, drawCalls,
        m_voxelWorld,
        m_wireframeOn,
        m_enableFrustumCulling,
        m_resourceMgr);
    m_uiRenderer->renderImGui(cmd);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    /* ---------- submit ---------- */
    VkSemaphore waitSem = m_frames[m_currentFrame].imageAvailableSemaphore;
    VkSemaphore signalSem = m_frames[m_currentFrame].renderFinishedSemaphore;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &waitSem;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &signalSem;

    if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submit,
        m_frames[m_currentFrame].inFlightFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Renderer: vkQueueSubmit failed");
    }

    /* ---------- present ---------- */
    VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &signalSem;
    VkSwapchainKHR sc = m_swapChain->getSwapChain();
    present.swapchainCount = 1;
    present.pSwapchains = &sc;
    present.pImageIndices = &imageIndex;

    res = vkQueuePresentKHR(m_context->getPresentQueue(), &present);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapChain();
    }
    else if (res != VK_SUCCESS)
    {
        throw std::runtime_error("Renderer: vkQueuePresentKHR failed");
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    /* ---------- CSV logging ---------- */
    s_frameCounter++;
    size_t cpuMem = Chunk::s_totalCPUBytes.load();
    size_t gpuMem = m_resourceMgr ? m_resourceMgr->GetTotalGPUBufferBytes() : 0;
    CpuProfiler::LogFrameStats(s_frameCounter, dt, fps, avgFps,
        cpuUse, avgCpu,
        drawCalls, totalVerts,
        cpuMem, gpuMem);
}

// ============================================================================
// MVP helpers
// ============================================================================
void Renderer::createMVPUniformBuffer()
{
    VkDeviceSize sz = sizeof(MVPBlock);

    createBuffer(sz,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_mvpBuffer,
        m_mvpMemory);

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };

    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_context->getDevice(), &poolInfo, nullptr,
        &m_mvpDescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Renderer: failed to create MVP descriptor pool");

    VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    alloc.descriptorPool = m_mvpDescriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &m_mvpLayout;

    if (vkAllocateDescriptorSets(m_context->getDevice(), &alloc,
        &m_mvpDescriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Renderer: failed to allocate MVP descriptor set");

    VkDescriptorBufferInfo bufInfo{ m_mvpBuffer, 0, sizeof(MVPBlock) };

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = m_mvpDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufInfo;

    vkUpdateDescriptorSets(m_context->getDevice(), 1, &write, 0, nullptr);
}

void Renderer::updateMVP()
{
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(45.0f),
        float(m_swapChain->getExtent().width) /
        float(m_swapChain->getExtent().height),
        0.1f, 100000.0f);
    proj[1][1] *= -1.0f;

    MVPBlock blk{ proj * view * model };

    void* data = nullptr;
    vkMapMemory(m_context->getDevice(), m_mvpMemory, 0,
        sizeof(MVPBlock), 0, &data);
    std::memcpy(data, &blk, sizeof(MVPBlock));
    vkUnmapMemory(m_context->getDevice(), m_mvpMemory);
}

// ============================================================================
// Low‑level buffer helper
// ============================================================================
void Renderer::createBuffer(VkDeviceSize sz,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props,
    VkBuffer& buf,
    VkDeviceMemory& mem)
{
    VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = sz;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &bci, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error("Renderer: vkCreateBuffer failed");

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_context->getDevice(), buf, &req);

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);

    if (vkAllocateMemory(m_context->getDevice(), &mai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("Renderer: vkAllocateMemory failed");

    vkBindBufferMemory(m_context->getDevice(), buf, mem, 0);
}

uint32_t Renderer::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((filter & (1 << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;

    throw std::runtime_error("Renderer: suitable memory type not found");
}

// ============================================================================
// recreateSwapChain  (unchanged logic)
// ============================================================================
void Renderer::recreateSwapChain()
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window->getGLFWwindow(), &w, &h);
    if (w == 0 || h == 0) return;  // minimized

    vkDeviceWaitIdle(m_context->getDevice());

    m_rpManager->cleanup();
    m_swapChain->cleanup();

    m_swapChain->init(m_context, m_window);

    m_rpManager->createRenderPass();
    m_rpManager->createFramebuffers();

    vkDestroyDescriptorSetLayout(m_context->getDevice(), m_mvpLayout, nullptr);
    m_mvpLayout = m_pipelineMgr->createMVPDescriptorSetLayout();

    auto ext = m_swapChain->getExtent();
    auto rp = m_rpManager->getRenderPass();

    m_pipelineMgr->createVoxelPipelineFill("voxel_fill", rp, ext, m_mvpLayout);
    m_pipelineMgr->createVoxelPipelineWireframe("voxel_wireframe", rp, ext, m_mvpLayout);

    if (m_mvpBuffer)         vkDestroyBuffer(m_context->getDevice(), m_mvpBuffer, nullptr);
    if (m_mvpMemory)         vkFreeMemory(m_context->getDevice(), m_mvpMemory, nullptr);
    if (m_mvpDescriptorPool) vkDestroyDescriptorPool(m_context->getDevice(), m_mvpDescriptorPool, nullptr);

    createMVPUniformBuffer();
}

// ============================================================================
// ════════════════════════════════════════════════════════════════════════════
// Renderer::MeshBatch (UPDATED SIGNATURES)
// ════════════════════════════════════════════════════════════════════════════
void Renderer::MeshBatch::ensureCapacity(Renderer* owner,
    VkDeviceSize wantVbo,
    VkDeviceSize wantIbo)
{
    VulkanContext* ctx = owner->m_context;
    VkDevice        dev = ctx->getDevice();

    auto destroyBuf = [&](VkBuffer& b)
        { if (b) { vkDestroyBuffer(dev, b, nullptr); b = VK_NULL_HANDLE; } };

    auto destroyMem = [&](VkDeviceMemory& m)
        { if (m) { vkFreeMemory(dev, m, nullptr); m = VK_NULL_HANDLE; } };

    // 1) grow vertex buffer if needed
    if (wantVbo > vboSize)
    {
        destroyBuf(vbo);
        destroyMem(memory);

        // vboSize = std::max(vboSize * 3 / 2 + 65536, wantVbo);
        vboSize = ((vboSize * 3 / 2 + 65536) > wantVbo)
            ? (vboSize * 3 / 2 + 65536)
            : wantVbo;

        owner->createBuffer(vboSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            vbo, memory);
    }

    // 2) grow index buffer if needed
    if (wantIbo > iboSize)
    {
        destroyBuf(ibo);

        // iboSize = std::max(iboSize * 3 / 2 + 65536, wantIbo);
        iboSize = ((iboSize * 3 / 2 + 65536) > wantIbo)
            ? (iboSize * 3 / 2 + 65536)
            : wantIbo;

        VkDeviceMemory dummy;
        owner->createBuffer(iboSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            ibo, dummy);
    }
}

uint32_t Renderer::MeshBatch::appendChunk(Renderer* owner,
    VkBuffer srcVbo, VkDeviceSize srcVboBytes,
    VkBuffer srcIbo, VkDeviceSize srcIboBytes)
{
    ensureCapacity(owner, vboUsed + srcVboBytes, iboUsed + srcIboBytes);

    ResourceManager* rm = owner->m_resourceMgr;

    VkBufferCopy region{};
    /* vertices */
    region.srcOffset = 0;
    region.dstOffset = vboUsed;
    region.size = srcVboBytes;
    rm->copyBufferRegions(srcVbo, vbo, &region, 1);

    /* indices */
    region.dstOffset = iboUsed;
    region.size = srcIboBytes;
    rm->copyBufferRegions(srcIbo, ibo, &region, 1);

    uint32_t firstIdx = static_cast<uint32_t>(iboUsed / sizeof(uint32_t));
    vboUsed += srcVboBytes;
    iboUsed += srcIboBytes;
    return firstIdx;
}
