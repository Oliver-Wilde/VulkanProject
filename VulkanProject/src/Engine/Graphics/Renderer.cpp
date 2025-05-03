#ifndef VK_CHECK
#define VK_CHECK(x)                                                              \
    do {                                                                         \
        VkResult _res = (x);                                                     \
        if (_res != VK_SUCCESS)                                                  \
            throw std::runtime_error("VK_CHECK failed at " __FILE__);            \
    } while (0)
#endif


#ifdef BENCHMARK_MODE
#include "Engine/Utils/BenchmarkLogger.h"
#endif
#include <chrono>    
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

#include "Engine/Utils/CpuProfiler.h" 

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
static CpuProfiler g_cpuProfiler;
static uint64_t    s_frameCounter = 0;
static constexpr uint64_t MAX_TL_FRAMES_AHEAD = 2;
static float s_lastGpuMs = 0.f;

static uint64_t fnv1a64(const void* d, size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(d);
    uint64_t h = 14695981039346656037ULL;
    while (n--) { h ^= *p++; h *= 1099511628211ULL; }
    return h;
}

namespace {
    static inline uint64_t fnvMix(uint64_t h, uint64_t v)
    {
        h ^= v; h *= 1099511628211ULL; return h;
    }

    static uint64_t calcGeomHash(const std::vector<Chunk*>& vis, bool wire)
    {
        uint64_t h = 14695981039346656037ULL;
        for (Chunk* c : vis)
        {
            h = fnvMix(h, reinterpret_cast<uint64_t>(c));
            h = fnvMix(h, reinterpret_cast<uint64_t>(c->getVertexBuffer()));
            h = fnvMix(h, reinterpret_cast<uint64_t>(c->getIndexBuffer()));
        }
        if (wire) h = fnvMix(h, 1ULL);
        return h;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GeometryBuilder  (private nested class)  IMPLEMENTATION
// ─────────────────────────────────────────────────────────────────────────────
Renderer::GeometryBuilder::GeometryBuilder(Renderer* owner)
    : m_owner(owner)
{
    CpuProfiler::ScopedTimer timer("Renderer::GeometryBuilder::GeometryBuilder");

    VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.queueFamilyIndex = m_owner->m_context->getGraphicsQueueFamilyIndex();
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(m_owner->m_context->getDevice(),
        &pci, nullptr, &m_cmdPool) != VK_SUCCESS)
        throw std::runtime_error("GeometryBuilder: command-pool create failed");

    m_thread = std::thread(&GeometryBuilder::threadMain, this);
}


Renderer::GeometryBuilder::~GeometryBuilder()
{
    CpuProfiler::ScopedTimer timer("Renderer::GeometryBuilder::~GeometryBuilder");
    VkDevice dev = m_owner->m_context->getDevice();
    vkDeviceWaitIdle(dev);

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_exit = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();

    while (!m_done.empty())
    {
        vkFreeCommandBuffers(dev, m_cmdPool, 1, &m_done.front().cmd);
        m_done.pop_front();
    }
    vkDestroyCommandPool(dev, m_cmdPool, nullptr);
}

void Renderer::GeometryBuilder::submit(const GeometryJob& job)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_jobs.push_back(job);
    }
    m_cv.notify_one();
}

VkCommandBuffer Renderer::GeometryBuilder::fetchFinished(uint32_t imgIdx,
    uint32_t& outVerts, uint32_t& outCalls, uint64_t& outHash)
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
            m_cv.wait(lk, [&] { return m_exit || !m_jobs.empty(); });
            if (m_exit && m_jobs.empty()) return;
            job = m_jobs.front();
            m_jobs.pop_front();
        }

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

        std::vector<Chunk*> vis;
        const auto& all = m_owner->m_voxelWorld->getChunkManager().getAllChunks();
        vis.reserve(all.size());
        for (const auto& kv : all)
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

        uint64_t hash = calcGeomHash(vis, job.wantWire);

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
            pInfo.pipelineLayout, 0, 1, &m_owner->m_mvpDescriptorSet, 0, nullptr);

        /* ── NEW: push sunlight direction ─────────────────────────────────── */
        float pc[4] = { m_owner->m_sunDir.x,
                        m_owner->m_sunDir.y,
                        m_owner->m_sunDir.z, 0.0f };
        vkCmdPushConstants(gcb, pInfo.pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc);

        VkDeviceSize zero = 0;
        uint32_t vertsTotal = 0;

        for (Chunk* c : vis)
        {
            VkBuffer vb = c->getVertexBuffer();
            vkCmdBindVertexBuffers(gcb, 0, 1, &vb, &zero);
            vkCmdBindIndexBuffer(gcb, c->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(gcb, c->getIndexCount(), 1, 0, 0, 0);
            vertsTotal += c->getVertexCount();
        }
        vkEndCommandBuffer(gcb);

        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_done.emplace_back(FinishedCB{ gcb, vertsTotal,
                static_cast<uint32_t>(vis.size()), job.imgIdx, hash, job.wantWire });
        }
        m_cv.notify_one();
    }
}

// ============================================================================
// Constructor / Destructor
// ============================================================================
Renderer::Renderer(VulkanContext* ctx, Window* wnd, VoxelWorld* world)
    : m_context(ctx)
    , m_window(wnd)
    , m_voxelWorld(world)
{
    // 1) swap‑chain & managers (unchanged)
    m_swapChain = new SwapChain();      m_swapChain->init(m_context, m_window);
    m_resourceMgr = new ResourceManager(m_context);
    m_pipelineMgr = new PipelineManager(m_context, m_resourceMgr);
    m_rpManager = new RenderPassManager(m_context, m_swapChain);
    m_rpManager->createRenderPass();
    m_rpManager->createFramebuffers();

    // 2) pipelines + layout
    auto extent = m_swapChain->getExtent();
    auto rp = m_rpManager->getRenderPass();
    m_mvpLayout = m_pipelineMgr->createMVPDescriptorSetLayout();
    m_pipelineMgr->createVoxelPipelineFill("voxel_fill", rp, extent, m_mvpLayout);
    m_pipelineMgr->createVoxelPipelineWireframe("voxel_wireframe", rp, extent, m_mvpLayout);

    // 3) uniform buffer
    createMVPUniformBuffer();

    // 4) primary command buffers, fences, binary sem (legacy) handled by …
    createSyncObjects();

    // 5) UI
    m_uiRenderer = new UIRenderer();
    m_uiRenderer->init(m_context, m_window, rp, m_swapChain->getImageCount());

    // 6) geometry secondary CB pool
    size_t imgCount = m_swapChain->getImageCount();
    m_geomCmd.resize(imgCount, VK_NULL_HANDLE);
    m_geomHash.resize(imgCount, 0);
    m_cachedVerts.resize(imgCount, 0);
    m_cachedCalls.resize(imgCount, 0);
    m_cachedWireframe.resize(imgCount, false);

    VkCommandBufferAllocateInfo secAI{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    secAI.commandPool = m_context->getCommandPool();
    secAI.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    secAI.commandBufferCount = 1;
    for (size_t i = 0; i < imgCount; ++i)
    {
        if (vkAllocateCommandBuffers(m_context->getDevice(), &secAI, &m_geomCmd[i]) != VK_SUCCESS)
            throw std::runtime_error("Renderer: secondary CB alloc failed");
    }

    // 7) async builder
    m_geoBuilder = std::make_unique<GeometryBuilder>(this);

    // 8) NEW: IndirectBatch global instance ---------------------------------
    m_indirectBatch = std::make_unique<gfx::IndirectBatch>();
    m_indirectBatch->init(m_context, m_resourceMgr);
}




Renderer::~Renderer()
{
    vkDeviceWaitIdle(m_context->getDevice());

    m_indirectBatch.reset();

    destroySyncObjects();

    if (!m_geomCmd.empty())
        vkFreeCommandBuffers(m_context->getDevice(), m_context->getCommandPool(),
            static_cast<uint32_t>(m_geomCmd.size()), m_geomCmd.data());

    if (m_uiRenderer) { m_uiRenderer->cleanup(); delete m_uiRenderer; }
    if (m_resourceMgr) { delete m_resourceMgr; }
    delete m_pipelineMgr; delete m_rpManager; delete m_swapChain;

    vkDestroyBuffer(m_context->getDevice(), m_mvpBuffer, nullptr);
    vkFreeMemory(m_context->getDevice(), m_mvpMemory, nullptr);
    vkDestroyDescriptorPool(m_context->getDevice(), m_mvpDescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_context->getDevice(), m_mvpLayout, nullptr);
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
    int safeIndex = (m_currentFrame + DESTROY_LATENCY)
        % (MAX_FRAMES_IN_FLIGHT + DESTROY_LATENCY);
    m_deferredFrees[safeIndex].push_back(q);
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


void Renderer::setSunDirection(const glm::vec3& dir)
{
    glm::vec3 n = glm::normalize(dir);
    if (glm::any(glm::isnan(n)) || glm::length(n) < 1e-4f)
        n = glm::vec3(0.f, -1.f, 0.f);
    m_sunDir = n;
}



// ============================================================================
// buildGeometryCB  – records / caches geometry secondary command buffer
// ============================================================================
uint64_t Renderer::buildGeometryCB(uint32_t imgIdx,
    const Frustum& fr,
    bool useCull,
    uint32_t& outVerts,
    uint32_t& outCalls)
{
    VkCommandBuffer gcb = m_geomCmd[imgIdx];

    // Visible-set gather (unchanged) …
    std::vector<Chunk*> vis;
    const auto& all = m_voxelWorld->getChunkManager().getAllChunks();
    vis.reserve(all.size());

    for (const auto& kv : all)
    {
        Chunk* c = kv.second.get();
        if (!c) continue;
        if (useCull)
        {
            glm::vec3 mn, mx; c->getBoundingBox(mn, mx);
            if (!fr.intersectsAABB(mn, mx)) continue;
        }
        if (!c->getVertexBuffer() || !c->getIndexBuffer()) continue;
        vis.push_back(c);
    }
    std::sort(vis.begin(), vis.end());

    uint64_t newHash = calcGeomHash(vis, m_wireframeOn);
    if (newHash == m_geomHash[imgIdx])
    {
        outVerts = m_cachedVerts[imgIdx];
        outCalls = m_cachedCalls[imgIdx];
        return newHash; // cache hit
    }

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
    auto pInfo = m_pipelineMgr->getPipeline(pipeName);
    vkCmdBindPipeline(gcb, VK_PIPELINE_BIND_POINT_GRAPHICS, pInfo.pipeline);
    vkCmdBindDescriptorSets(gcb, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pInfo.pipelineLayout, 0, 1, &m_mvpDescriptorSet, 0, nullptr);

    /* NEW: push light dir */
    float pc[4] = { m_sunDir.x, m_sunDir.y, m_sunDir.z, 0.f };
    vkCmdPushConstants(gcb, pInfo.pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc);

    VkDeviceSize zero = 0;
    uint32_t     vertsTotal = 0;

    for (Chunk* c : vis)
    {
        VkBuffer vb = c->getVertexBuffer();
        vkCmdBindVertexBuffers(gcb, 0, 1, &vb, &zero);
        vkCmdBindIndexBuffer(gcb, c->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(gcb, c->getIndexCount(), 1, 0, 0, 0);
        vertsTotal += c->getVertexCount();
    }

    vkEndCommandBuffer(gcb);

    m_geomHash[imgIdx] = newHash;
    m_cachedVerts[imgIdx] = vertsTotal;
    m_cachedCalls[imgIdx] = static_cast<uint32_t>(vis.size());
    m_cachedWireframe[imgIdx] = m_wireframeOn;

    outVerts = vertsTotal;
    outCalls = static_cast<uint32_t>(vis.size());
    return newHash;
}
// ============================================================================
// renderFrame  (unchanged logic; only comments trimmed)
// ============================================================================
void Renderer::renderFrame()
{
    CpuProfiler::ScopedTimer ft("Renderer::renderFrame");
    updateMVP();

    /* timeline-semaphore throttling (unchanged) */
    if (m_useTimelineSemaphores)
    {
        FrameResources& oldest = m_frames[(m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT];
        if (oldest.timelineSemaphore)
        {
            uint64_t gpuDone = 0;
            vkGetSemaphoreCounterValue(m_context->getDevice(),
                oldest.timelineSemaphore, &gpuDone);
            if (oldest.timelineValue - gpuDone >= MAX_TL_FRAMES_AHEAD)
            {
                VkSemaphoreWaitInfo wi{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
                wi.semaphoreCount = 1;
                wi.pSemaphores = &oldest.timelineSemaphore;
                uint64_t waitVal = oldest.timelineValue;
                wi.pValues = &waitVal;
                vkWaitSemaphores(m_context->getDevice(), &wi, UINT64_MAX);
            }
        }
    }

    FrameResources& fr = m_frames[m_currentFrame];

    /* ── wait for fence from N-2 frame ─────────────────────────────────── */
    vkWaitForFences(m_context->getDevice(), 1, &fr.inFlightFence,
        VK_TRUE, UINT64_MAX);
    vkResetFences(m_context->getDevice(), 1, &fr.inFlightFence);

    /* ── read GPU time from frame that just finished ──────────────────── */
    {
        uint32_t done = (m_currentFrame + MAX_FRAMES_IN_FLIGHT - 1)
            % MAX_FRAMES_IN_FLIGHT;
        uint64_t ts[2]{};
        if (vkGetQueryPoolResults(m_context->getDevice(), m_timestampPool,
            done * 2, 2, sizeof(ts), ts, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS)
        {
            double ns = double(ts[1] - ts[0]) * double(m_timestampPeriod);
            s_lastGpuMs = float(ns / 1.0e6);      // cache for CSV
        }
    }

    freeDeferredResources();
    m_resourceMgr->flushUploads(false);

    /* ── acquire swap-chain image ─────────────────────────────────────── */
    uint32_t imgIdx = 0;
    VkResult acq = vkAcquireNextImageKHR(m_context->getDevice(),
        m_swapChain->getSwapChain(), UINT64_MAX,
        fr.imageAvailableSemaphore, VK_NULL_HANDLE, &imgIdx);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapChain(); return;
    }
    else if (acq != VK_SUCCESS)
        throw std::runtime_error("vkAcquireNextImageKHR failed");

    /* ── build / fetch geometry secondary CB ──────────────────────────── */
    Frustum frustum;
    if (m_enableFrustumCulling)
        frustum = buildCameraFrustum(m_camera, m_swapChain->getExtent());

    uint32_t verts = 0, draws = 0;
    buildGeometryCB(imgIdx, frustum, m_enableFrustumCulling, verts, draws);

    /* ── record primary CB ────────────────────────────────────────────── */
    VkCommandBuffer cmd = fr.commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    /* reset + BEGIN timestamp */
    uint32_t qFirst = m_currentFrame * 2;
    vkCmdResetQueryPool(cmd, m_timestampPool, qFirst, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        m_timestampPool, qFirst);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    VkClearValue clr[2];
    clr[0].color = { {0.1f, 0.2f, 0.3f, 1.f} };
    clr[1].depthStencil = { 1.f, 0 };
    VkRenderPassBeginInfo rp{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp.renderPass = m_rpManager->getRenderPass();
    rp.framebuffer = m_rpManager->getFramebuffers()[imgIdx];
    rp.renderArea = { {0, 0}, m_swapChain->getExtent() };
    rp.clearValueCount = 2;
    rp.pClearValues = clr;

    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdExecuteCommands(cmd, 1, &m_geomCmd[imgIdx]);

    /* UI */
    float dt = m_time ? m_time->getDeltaTime() : 1000.f;
    float fps = dt > 0.f ? 1.f / dt : 0.f;
    float cpu = g_cpuProfiler.GetCpuUsage();
    m_uiRenderer->beginFrame();
    m_uiRenderer->renderDebugWindow(dt, fps, fps, cpu, cpu,
        verts, draws, m_voxelWorld, m_wireframeOn,
        m_enableFrustumCulling, m_resourceMgr);
    m_uiRenderer->renderImGui(cmd);

    vkCmdEndRenderPass(cmd);

    /* END timestamp */
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        m_timestampPool, qFirst + 1);

    VK_CHECK(vkEndCommandBuffer(cmd));

    /* ── submit & present (unchanged) ─────────────────────────────────── */
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &fr.imageAvailableSemaphore;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    VkSemaphore sig[2] = { fr.renderFinishedSemaphore, fr.timelineSemaphore };
    si.signalSemaphoreCount = m_useTimelineSemaphores ? 2 : 1;
    si.pSignalSemaphores = sig;

    /* timeline info if enabled */
    uint64_t nextTL = ++fr.timelineValue;
    VkTimelineSemaphoreSubmitInfo tlInfo{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    uint64_t waitVal = 0, sigVal[2] = { 0, nextTL };
    tlInfo.waitSemaphoreValueCount = 1;
    tlInfo.pWaitSemaphoreValues = &waitVal;
    tlInfo.signalSemaphoreValueCount = m_useTimelineSemaphores ? 2 : 1;
    tlInfo.pSignalSemaphoreValues = sigVal;
    si.pNext = m_useTimelineSemaphores ? &tlInfo : nullptr;

    VK_CHECK(vkQueueSubmit(m_context->getGraphicsQueue(), 1, &si,
        fr.inFlightFence));

    VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &fr.renderFinishedSemaphore;
    VkSwapchainKHR sc = m_swapChain->getSwapChain();
    pi.swapchainCount = 1;
    pi.pSwapchains = &sc;
    pi.pImageIndices = &imgIdx;

    VkResult pres = vkQueuePresentKHR(m_context->getPresentQueue(), &pi);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR)
        recreateSwapChain();
    else if (pres != VK_SUCCESS)
        throw std::runtime_error("vkQueuePresentKHR failed");

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

#ifdef BENCHMARK_MODE
    using clk = std::chrono::steady_clock;
    static const auto appStart = clk::now();

    BenchmarkLogger::FrameLogRow row{};
    row.frameNumber = static_cast<uint32_t>(s_frameCounter);
    row.timestampMs = std::chrono::duration<double, std::milli>(
        clk::now() - appStart).count();
    row.dtMs = dt * 1000.0f;

    /* now fully wired */
    row.cpuRebuildMs = m_voxelWorld->getCpuMeshingMsLastFrame();
    row.gpuBusyMs = s_lastGpuMs;
    row.bytesUploaded = m_resourceMgr->getBytesUploadedThisFrame();
    row.uploadBudget = m_voxelWorld->getUploadBudgetBytes();
    row.chunksRebuilt = m_voxelWorld->getChunksRebuiltLastFrame();

    row.drawCalls = draws;
    row.triangles = verts / 3;

    row.vramLiveBytes = m_resourceMgr->GetTotalGPUBufferBytes();
    row.cpuMemBytes = Chunk::s_totalCPUBytes.load(std::memory_order_relaxed);

    BenchmarkLogger::get().logFrame(row);
#endif /* BENCHMARK_MODE */


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

void Renderer::createSyncObjects()
{
    VkDevice dev = m_context->getDevice();

    /* legacy binary semaphore + fence configs */
    VkSemaphoreCreateInfo binCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo      fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    /* timeline semaphore config */
    VkSemaphoreTypeCreateInfo tlType{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    tlType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    tlType.initialValue = 0;
    VkSemaphoreCreateInfo tlCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    tlCI.pNext = &tlType;

    /* ── GPU-timestamp query-pool (instrumentation) ──────────────────── */
    {
        VkQueryPoolCreateInfo qp{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qp.queryCount = MAX_FRAMES_IN_FLIGHT * 2;          // begin + end / frame
        VK_CHECK(vkCreateQueryPool(dev, &qp, nullptr, &m_timestampPool));

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_context->getPhysicalDevice(), &props);
        m_timestampPeriod = props.limits.timestampPeriod;  // ns per query tick
    }

    /* per-flight resources ------------------------------------------------ */
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        /* primary CMD buffer */
        VkCommandBufferAllocateInfo cai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cai.commandPool = m_context->getCommandPool();
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(dev, &cai, &m_frames[i].commandBuffer));

        /* binary semaphores (swap-chain acquire / present) */
        VK_CHECK(vkCreateSemaphore(dev, &binCI, nullptr, &m_frames[i].imageAvailableSemaphore));
        VK_CHECK(vkCreateSemaphore(dev, &binCI, nullptr, &m_frames[i].renderFinishedSemaphore));

        /* timeline semaphore */
        if (m_useTimelineSemaphores)
            VK_CHECK(vkCreateSemaphore(dev, &tlCI, nullptr, &m_frames[i].timelineSemaphore));

        /* in-flight fence */
        VK_CHECK(vkCreateFence(dev, &fenceCI, nullptr, &m_frames[i].inFlightFence));
    }
}


void Renderer::destroySyncObjects()
{
    VkDevice dev = m_context->getDevice();

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroySemaphore(dev, m_frames[i].imageAvailableSemaphore, nullptr);
        vkDestroySemaphore(dev, m_frames[i].renderFinishedSemaphore, nullptr);

        if (m_useTimelineSemaphores && m_frames[i].timelineSemaphore)
            vkDestroySemaphore(dev, m_frames[i].timelineSemaphore, nullptr);

        vkDestroyFence(dev, m_frames[i].inFlightFence, nullptr);
        vkFreeCommandBuffers(dev, m_context->getCommandPool(), 1,
            &m_frames[i].commandBuffer);
    }

    /* instrumentation query-pool */
    if (m_timestampPool)
        vkDestroyQueryPool(dev, m_timestampPool, nullptr);
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

    // --- NEW: wait only for per-frame fences instead of full device idle ---
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        vkWaitForFences(m_context->getDevice(), 1,
            &m_frames[i].inFlightFence, VK_TRUE, UINT64_MAX);

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
std::pair<uint32_t, uint32_t> Renderer::MeshBatch::appendChunk(
    Renderer* owner,
    VkBuffer srcVbo, VkDeviceSize srcVboBytes,
    VkBuffer srcIbo, VkDeviceSize srcIboBytes)
{
    // baseVertex is vertex offset BEFORE copy in vertex units (20‑byte stride)
    uint32_t baseVertex = static_cast<uint32_t>(vboUsed / sizeof(Vertex));
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
    return { firstIdx, baseVertex };
}


#ifdef BENCHMARK_MODE
#include <string>
std::string Renderer::queryHardwareString()
{
    return "unknown";        // simple stub; refine later if desired
}
#endif