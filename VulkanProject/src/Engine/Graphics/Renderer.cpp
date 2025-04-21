#include "Renderer.h"
#include "Engine/Graphics/Frustum.h"
#include "Engine/Core/Window.h"
#include "Engine/Core/Time.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Graphics/SwapChain.h"
#include "Engine/Graphics/RenderPassManager.h"
#include "Engine/Graphics/PipelineManager.h"
#include "Engine/Resources/ResourceManager.h"


#include <cstring>
#include "UIRenderer.h"

#include "Engine/Voxels/VoxelWorld.h"
#include "Engine/Voxels/Chunk.h"
#include "Engine/Scene/Camera.h"


#include "../Utils/CpuProfiler.h"
#include <Engine/Utils/Logger.h>
#include <Engine/Utils/ThreadPool.h>
#include <numeric>   
#include <stdexcept>
#include <deque>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#undef  max
#undef  min

extern ThreadPool g_threadPool;
static CpuProfiler g_cpuProfiler;              // global CPU profiler
static uint64_t    s_frameCounter = 0;         // running frame counter

static uint64_t fnv1a64(const void* d, size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(d);
    uint64_t h = 14695981039346656037ULL;
    while (n--) { h ^= *p++; h *= 1099511628211ULL; }
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// GeometryBuilder  (private nested class)  IMPLEMENTATION
// ─────────────────────────────────────────────────────────────────────────────
Renderer::GeometryBuilder::GeometryBuilder(Renderer* owner)
    : m_owner(owner)
{
    // create a dedicated command pool for this thread (graphics family)
    VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.queueFamilyIndex = m_owner->m_context->getGraphicsQueueFamilyIndex();
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(m_owner->m_context->getDevice(), &pci, nullptr, &m_cmdPool) != VK_SUCCESS)
        throw std::runtime_error("GeometryBuilder: command‑pool create failed");

    m_thread = std::thread(&GeometryBuilder::threadMain, this);
}

Renderer::GeometryBuilder::~GeometryBuilder()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_exit = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();

    // free any leftover command buffers
    while (!m_done.empty())
    {
        vkFreeCommandBuffers(m_owner->m_context->getDevice(), m_cmdPool, 1, &m_done.front().cmd);
        m_done.pop_front();
    }
    while (!m_jobs.empty()) m_jobs.pop_front();                           // ◆ PATCH

    vkDestroyCommandPool(m_owner->m_context->getDevice(), m_cmdPool, nullptr);
}

void Renderer::GeometryBuilder::submit(const GeometryJob& job)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_jobs.push_back(job);                                            // ◆ PATCH
    }
    m_cv.notify_one();
}

VkCommandBuffer Renderer::GeometryBuilder::fetchFinished(uint32_t imgIdx,
    uint32_t& outVerts,
    uint32_t& outCalls,
    uint64_t& outHash)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto it = m_done.begin(); it != m_done.end(); ++it)
    {
        if (it->imgIdx == imgIdx)
        {
            FinishedCB res = *it;
            m_done.erase(it);
            outVerts = res.verts;
            outCalls = res.calls;
            outHash = res.hash;
            return res.cmd;
        }
    }
    return VK_NULL_HANDLE;
}

void Renderer::GeometryBuilder::threadMain()
{
    while (true)
    {
        GeometryJob job;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [&]() { return m_exit || !m_jobs.empty(); });
            if (m_exit && m_jobs.empty()) return;
            job = m_jobs.front();
            m_jobs.pop_front();
        }

        // allocate a secondary command buffer for this job
        VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ai.commandPool = m_cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer gcb;
        if (vkAllocateCommandBuffers(m_owner->m_context->getDevice(), &ai, &gcb) != VK_SUCCESS)
        {
            Logger::Error("GeometryBuilder: CMD alloc failed");
            continue;
        }

        // gather visible chunks (same logic as main‑thread path)
        std::vector<Chunk*> vis;
        const auto& all = m_owner->m_voxelWorld->getChunkManager().getAllChunks();
        vis.reserve(all.size());
        for (auto& kv : all)
        {
            Chunk* c = kv.second.get();
            if (!c) continue;
            if (job.useCulling)
            {
                glm::vec3 mn, mx; c->getBoundingBox(mn, mx);
                if (!job.frustum.intersectsAABB(mn, mx)) continue;
            }
            if (!c->getVertexBuffer() || !c->getIndexBuffer()) continue;
            vis.push_back(c);
        }
        std::sort(vis.begin(), vis.end());

        uint64_t hash = fnv1a64(vis.data(), vis.size() * sizeof(Chunk*));
        hash ^= (job.wantWire ? 0x1 : 0x0);

        // record CB
        VkCommandBufferInheritanceInfo inh{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
        inh.renderPass = m_owner->m_rpManager->getRenderPass();
        inh.subpass = 0;
        inh.framebuffer = m_owner->m_rpManager->getFramebuffers()[job.imgIdx];

        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        bi.pInheritanceInfo = &inh;
        vkBeginCommandBuffer(gcb, &bi);

        const std::string pipeName = job.wantWire ? "voxel_wireframe" : "voxel_fill";
        auto pInfo = m_owner->m_pipelineMgr->getPipeline(pipeName);
        vkCmdBindPipeline(gcb, VK_PIPELINE_BIND_POINT_GRAPHICS, pInfo.pipeline);
        vkCmdBindDescriptorSets(gcb, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pInfo.pipelineLayout, 0, 1,
            &m_owner->m_mvpDescriptorSet, 0, nullptr);

        Renderer::MeshBatch batchTemp;                   // standalone batch (thread‑local)
        uint32_t runningVert = 0, vertsTotal = 0;
        std::vector<uint32_t> firstIndices;
        std::vector<uint32_t> indexCounts;
        firstIndices.reserve(vis.size());
        indexCounts.reserve(vis.size());

        for (Chunk* c : vis)
        {
            VkDeviceSize vbBytes = c->getVertexCount() * sizeof(Vertex);
            VkDeviceSize ibBytes = c->getIndexCount() * sizeof(uint32_t);

            batchTemp.ensureCapacity(m_owner, batchTemp.vboUsed + vbBytes, batchTemp.iboUsed + ibBytes);
            uint32_t firstIdx = batchTemp.appendChunk(m_owner, c->getVertexBuffer(), vbBytes,
                c->getIndexBuffer(), ibBytes);
            firstIndices.push_back(firstIdx);
            indexCounts.push_back(c->getIndexCount());
            runningVert += c->getVertexCount();
            vertsTotal += c->getVertexCount();
        }

        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(gcb, 0, 1, &batchTemp.vbo, &zero);
        vkCmdBindIndexBuffer(gcb, batchTemp.ibo, 0, VK_INDEX_TYPE_UINT32);
        for (size_t i = 0; i < firstIndices.size(); ++i)
            vkCmdDrawIndexed(gcb, indexCounts[i], 1, firstIndices[i], 0, 0);

        vkEndCommandBuffer(gcb);

        // store result
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_done.push_back({ gcb, vertsTotal, static_cast<uint32_t>(firstIndices.size()), job.imgIdx, hash, job.wantWire });
        }
        m_cv.notify_one();
    }
}


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

    // ── 8) NEW – geometry secondary command buffers  ░░░░░░░░░░░░░░░░░░░░░░░░░
    size_t imgCount = m_swapChain->getImageCount();

    m_geomCmd.resize(imgCount, VK_NULL_HANDLE);
    m_geomHash.resize(imgCount, 0);
    m_cachedVerts.resize(imgCount, 0);
    m_cachedCalls.resize(imgCount, 0);
    m_cachedWireframe.resize(imgCount, false);  

    VkCommandBufferAllocateInfo secAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    secAlloc.commandPool = m_context->getCommandPool();
    secAlloc.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    secAlloc.commandBufferCount = 1;

    for (size_t i = 0; i < imgCount; ++i)
    {
        if (vkAllocateCommandBuffers(m_context->getDevice(),
            &secAlloc,
            &m_geomCmd[i]) != VK_SUCCESS)
            throw std::runtime_error("Renderer: secondary CB alloc failed");
    }

}



Renderer::~Renderer()
{
    vkDeviceWaitIdle(m_context->getDevice());

    // Secondary CBs
    if (!m_geomCmd.empty())
        vkFreeCommandBuffers(m_context->getDevice(),
            m_context->getCommandPool(),
            static_cast<uint32_t>(m_geomCmd.size()),
            m_geomCmd.data());

    // Per‑flight resources
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (m_frames[i].commandBuffer)
            vkFreeCommandBuffers(m_context->getDevice(),
                m_context->getCommandPool(), 1, &m_frames[i].commandBuffer);
        if (m_frames[i].imageAvailableSemaphore)
            vkDestroySemaphore(m_context->getDevice(),
                m_frames[i].imageAvailableSemaphore, nullptr);
        if (m_frames[i].renderFinishedSemaphore)
            vkDestroySemaphore(m_context->getDevice(),
                m_frames[i].renderFinishedSemaphore, nullptr);
        if (m_frames[i].inFlightFence)
            vkDestroyFence(m_context->getDevice(),
                m_frames[i].inFlightFence, nullptr);
    }

    // Uniform resources
    if (m_mvpBuffer)          vkDestroyBuffer(m_context->getDevice(), m_mvpBuffer, nullptr);
    if (m_mvpMemory)          vkFreeMemory(m_context->getDevice(), m_mvpMemory, nullptr);
    if (m_mvpDescriptorPool)  vkDestroyDescriptorPool(m_context->getDevice(), m_mvpDescriptorPool, nullptr);
    if (m_mvpLayout)          vkDestroyDescriptorSetLayout(m_context->getDevice(), m_mvpLayout, nullptr);

    // UI
    if (m_uiRenderer) { m_uiRenderer->cleanup(); delete m_uiRenderer; m_uiRenderer = nullptr; }

    // Managers
    delete m_rpManager;   m_rpManager = nullptr;
    delete m_pipelineMgr; m_pipelineMgr = nullptr;
    delete m_resourceMgr; m_resourceMgr = nullptr;

    // Swap‑chain
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
    return std::accumulate(buf.begin(), buf.end(), 0.f) / buf.size();
}


// ============================================================================
// buildGeometryCB  – records / caches geometry secondary command buffer
// ============================================================================
uint64_t Renderer::buildGeometryCB(uint32_t imgIdx,
    const Frustum& fr,
    bool           useCull,
    uint32_t& outVerts,
    uint32_t& outCalls)
{
    VkCommandBuffer gcb = m_geomCmd[imgIdx];

    // 1) gather visible chunks
    std::vector<Chunk*> vis;
    const auto& all = m_voxelWorld->getChunkManager().getAllChunks();
    vis.reserve(all.size());

    for (auto& kv : all)
    {
        Chunk* c = kv.second.get();
        if (!c) continue;

        if (useCull)
        {
            glm::vec3 mn, mx;
            c->getBoundingBox(mn, mx);
            if (!fr.intersectsAABB(mn, mx)) continue;
        }

        if (!c->getVertexBuffer() || !c->getIndexBuffer()) continue;
        vis.push_back(c);
    }
    std::sort(vis.begin(), vis.end());                         // stable hash list

    // 2) cache test
    uint64_t newHash = fnv1a64(vis.data(), vis.size() * sizeof(Chunk*));
    if (newHash == m_geomHash[imgIdx] &&
        m_wireframeOn == m_cachedWireframe[imgIdx])
    {
        outVerts = m_cachedVerts[imgIdx];
        outCalls = m_cachedCalls[imgIdx];
        return newHash;                                        // cache hit
    }

    // 3) record secondary CB
    vkResetCommandBuffer(gcb, 0);

    VkCommandBufferInheritanceInfo inh{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
    inh.renderPass = m_rpManager->getRenderPass();
    inh.subpass = 0;
    inh.framebuffer = m_rpManager->getFramebuffers()[imgIdx];

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    bi.pInheritanceInfo = &inh;
    vkBeginCommandBuffer(gcb, &bi);

    const std::string pipeName = m_wireframeOn ? "voxel_wireframe" : "voxel_fill";
    const auto        pInfo = m_pipelineMgr->getPipeline(pipeName);

    vkCmdBindPipeline(gcb, VK_PIPELINE_BIND_POINT_GRAPHICS, pInfo.pipeline);
    vkCmdBindDescriptorSets(gcb, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pInfo.pipelineLayout, 0, 1,
        &m_mvpDescriptorSet, 0, nullptr);

    // ---- mesh‑batch path ----------------------------------------------------
    MeshBatch& batch = m_batches[m_currentFrame];
    batch.reset();

    struct DrawInfo { uint32_t idxCount, firstIdx, baseVert; };
    std::vector<DrawInfo> draws;
    draws.reserve(vis.size());

    uint32_t runningVert = 0;
    uint32_t vertsTotal = 0;

    for (Chunk* c : vis)
    {
        VkDeviceSize vbBytes = c->getVertexCount() * sizeof(Vertex);
        VkDeviceSize ibBytes = c->getIndexCount() * sizeof(uint32_t);

        uint32_t firstIdx = batch.appendChunk(this,
            c->getVertexBuffer(), vbBytes,
            c->getIndexBuffer(), ibBytes);

        draws.push_back({ c->getIndexCount(), firstIdx, runningVert });
        runningVert += c->getVertexCount();
        vertsTotal += c->getVertexCount();
    }

    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(gcb, 0, 1, &batch.vbo, &zero);
    vkCmdBindIndexBuffer(gcb, batch.ibo, 0, VK_INDEX_TYPE_UINT32);

    for (const auto& d : draws)
        vkCmdDrawIndexed(gcb, d.idxCount, 1, d.firstIdx,
            static_cast<int32_t>(d.baseVert), 0);

    vkEndCommandBuffer(gcb);

    // 4) update cache
    m_geomHash[imgIdx] = newHash;
    m_cachedVerts[imgIdx] = vertsTotal;
    m_cachedCalls[imgIdx] = static_cast<uint32_t>(draws.size());
    m_cachedWireframe[imgIdx] = m_wireframeOn;

    outVerts = vertsTotal;
    outCalls = static_cast<uint32_t>(draws.size());
    return newHash;
}

// ============================================================================
// renderFrame  (unchanged logic; only comments trimmed)
// ============================================================================
void Renderer::renderFrame()
{
    CpuProfiler::ScopedTimer ft("Renderer::renderFrame");
    updateMVP();

    // wait for previous frame
    vkWaitForFences(m_context->getDevice(), 1,
        &m_frames[m_currentFrame].inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_context->getDevice(), 1,
        &m_frames[m_currentFrame].inFlightFence);

    freeDeferredResources();

    m_resourceMgr->flushUploads(false);

    // acquire next image
    uint32_t imgIdx;
    VkResult res = vkAcquireNextImageKHR(
        m_context->getDevice(),
        m_swapChain->getSwapChain(),
        UINT64_MAX,
        m_frames[m_currentFrame].imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imgIdx);

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapChain(); return;
    }
    else if (res != VK_SUCCESS)
        throw std::runtime_error("Renderer: vkAcquireNextImageKHR failed");

    // frustum
    Frustum fr;
    if (m_enableFrustumCulling)
        fr = buildCameraFrustum(m_camera, m_swapChain->getExtent());

    // geometry secondary CB
    uint32_t verts = 0, draws = 0;
    buildGeometryCB(imgIdx, fr, m_enableFrustumCulling, verts, draws);

    // record primary CB
    VkCommandBuffer cmd = m_frames[m_currentFrame].commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue clear[2];
    clear[0].color = { {0.1f, 0.2f, 0.3f, 1.f} };
    clear[1].depthStencil = { 1.f, 0 };

    VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp.renderPass = m_rpManager->getRenderPass();
    rp.framebuffer = m_rpManager->getFramebuffers()[imgIdx];
    rp.renderArea = { {0,0}, m_swapChain->getExtent() };
    rp.clearValueCount = 2;
    rp.pClearValues = clear;

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdExecuteCommands(cmd, 1, &m_geomCmd[imgIdx]);

    // UI
    float dt = m_time ? m_time->getDeltaTime() : 0.f;
    float fps = dt > 0.f ? 1.f / dt : 0.f;
    float cpu = g_cpuProfiler.GetCpuUsage();

    m_uiRenderer->beginFrame();
    m_uiRenderer->renderDebugWindow(dt, fps, fps,
        cpu, cpu, verts, draws,
        m_voxelWorld,
        m_wireframeOn, m_enableFrustumCulling,
        m_resourceMgr);
    m_uiRenderer->renderImGui(cmd);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // submit
    VkSemaphore wait = m_frames[m_currentFrame].imageAvailableSemaphore;
    VkSemaphore signal = m_frames[m_currentFrame].renderFinishedSemaphore;
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo sub{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    sub.waitSemaphoreCount = 1;
    sub.pWaitSemaphores = &wait;
    sub.pWaitDstStageMask = &stage;
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    sub.signalSemaphoreCount = 1;
    sub.pSignalSemaphores = &signal;

    if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &sub,
        m_frames[m_currentFrame].inFlightFence) != VK_SUCCESS)
        throw std::runtime_error("Renderer: vkQueueSubmit failed");

    // present
    VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &signal;
    VkSwapchainKHR sc = m_swapChain->getSwapChain();
    pi.swapchainCount = 1;
    pi.pSwapchains = &sc;
    pi.pImageIndices = &imgIdx;

    res = vkQueuePresentKHR(m_context->getPresentQueue(), &pi);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
        recreateSwapChain();
    else if (res != VK_SUCCESS)
        throw std::runtime_error("Renderer: vkQueuePresentKHR failed");

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    // CSV logging
    s_frameCounter++;
    CpuProfiler::LogFrameStats(s_frameCounter, dt, fps, fps,
        cpu, cpu, draws, verts,
        Chunk::s_totalCPUBytes.load(std::memory_order_relaxed),
        m_resourceMgr->GetTotalGPUBufferBytes());
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

    VkDescriptorPoolCreateInfo dpi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_context->getDevice(), &dpi, nullptr,
        &m_mvpDescriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Renderer: MVP descriptor pool failed");

    VkDescriptorSetAllocateInfo dsa{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsa.descriptorPool = m_mvpDescriptorPool;
    dsa.descriptorSetCount = 1;
    dsa.pSetLayouts = &m_mvpLayout;

    if (vkAllocateDescriptorSets(m_context->getDevice(), &dsa, &m_mvpDescriptorSet) != VK_SUCCESS)
        throw std::runtime_error("Renderer: MVP descriptor set alloc failed");

    VkDescriptorBufferInfo buf{ m_mvpBuffer, 0, sizeof(MVPBlock) };

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = m_mvpDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &buf;

    vkUpdateDescriptorSets(m_context->getDevice(), 1, &write, 0, nullptr);
}


void Renderer::updateMVP()
{
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(45.0f),
        float(m_swapChain->getExtent().width) /
        float(m_swapChain->getExtent().height), 0.1f, 100000.0f);
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
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
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
    if (w == 0 || h == 0) return;

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

    vkDestroyBuffer(m_context->getDevice(), m_mvpBuffer, nullptr);
    vkFreeMemory(m_context->getDevice(), m_mvpMemory, nullptr);
    vkDestroyDescriptorPool(m_context->getDevice(), m_mvpDescriptorPool, nullptr);

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
    VkDevice       dev = ctx->getDevice();

    auto destroyBuf = [&](VkBuffer& b)
        { if (b) { vkDestroyBuffer(dev, b, nullptr); b = VK_NULL_HANDLE; } };
    auto destroyMem = [&](VkDeviceMemory& m)
        { if (m) { vkFreeMemory(dev, m, nullptr); m = VK_NULL_HANDLE; } };

    /* grow VBO if needed */
    if (wantVbo > vboSize)
    {
        destroyBuf(vbo);
        destroyMem(vboMemory);

        vboSize = std::max(vboSize * 3 / 2 + 65536, wantVbo);
        owner->createBuffer(vboSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            vbo, vboMemory);
    }

    /* grow IBO if needed */
    if (wantIbo > iboSize)
    {
        destroyBuf(ibo);
        destroyMem(iboMemory);

        iboSize = std::max(iboSize * 3 / 2 + 65536, wantIbo);
        owner->createBuffer(iboSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            ibo, iboMemory);      // ← store allocation!
    }
}
uint32_t Renderer::MeshBatch::appendChunk(Renderer* owner,
    VkBuffer srcVbo, VkDeviceSize srcVboBytes,
    VkBuffer srcIbo, VkDeviceSize srcIboBytes)
{
    ensureCapacity(owner, vboUsed + srcVboBytes, iboUsed + srcIboBytes);

    ResourceManager* rm = owner->m_resourceMgr;

    VkBufferCopy region{};
    // vertices
    region.srcOffset = 0;
    region.dstOffset = vboUsed;
    region.size = srcVboBytes;
    rm->copyBufferRegions(srcVbo, vbo, &region, 1);   // BLOCKING copy

    // indices
    region.dstOffset = iboUsed;
    region.size = srcIboBytes;
    rm->copyBufferRegions(srcIbo, ibo, &region, 1);

    uint32_t firstIdx = static_cast<uint32_t>(iboUsed / sizeof(uint32_t));
    vboUsed += srcVboBytes;
    iboUsed += srcIboBytes;
    return firstIdx;
}

