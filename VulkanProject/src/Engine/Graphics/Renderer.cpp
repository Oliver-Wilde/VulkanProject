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
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

extern ThreadPool g_threadPool;
static CpuProfiler g_cpuProfiler; // global CPU profiler

Renderer::Renderer(VulkanContext* context, Window* window, VoxelWorld* voxelWorld)
    : m_context(context)
    , m_window(window)
    , m_voxelWorld(voxelWorld)
{
    // 1) Create swap chain
    m_swapChain = new SwapChain();
    m_swapChain->init(m_context, m_window);

    // 2) Create managers
    m_resourceMgr = new ResourceManager(m_context);
    m_pipelineMgr = new PipelineManager(m_context, m_resourceMgr);
    m_rpManager = new RenderPassManager(m_context, m_swapChain);

    // 3) Create render pass + framebuffers
    m_rpManager->createRenderPass();
    m_rpManager->createFramebuffers();

    // 4) Create pipelines (fill & wireframe) + MVP descriptor layout
    auto extent = m_swapChain->getExtent();
    auto renderPass = m_rpManager->getRenderPass();

    m_mvpLayout = m_pipelineMgr->createMVPDescriptorSetLayout();
    m_pipelineMgr->createVoxelPipelineFill("voxel_fill", renderPass, extent, m_mvpLayout);
    m_pipelineMgr->createVoxelPipelineWireframe("voxel_wireframe", renderPass, extent, m_mvpLayout);

    // 5) Create MVP uniform buffer
    createMVPUniformBuffer();

    // 6) Create per-frame resources (command buffers, semaphores, fences)
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
            throw std::runtime_error("Failed to allocate cmd buffer for frame " + std::to_string(i));
        }

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (vkCreateSemaphore(m_context->getDevice(), &semInfo, nullptr, &m_frames[i].imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(m_context->getDevice(), &semInfo, nullptr, &m_frames[i].renderFinishedSemaphore) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create semaphores for frame " + std::to_string(i));
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr, &m_frames[i].inFlightFence) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create fence for frame " + std::to_string(i));
        }
    }

    // 7) Create and initialize the UIRenderer
    m_uiRenderer = new UIRenderer();
    m_uiRenderer->init(
        m_context,
        m_window,
        m_rpManager->getRenderPass(),
        m_swapChain->getImageCount()
    );
}

Renderer::~Renderer()
{
    vkDeviceWaitIdle(m_context->getDevice());

    // Per-frame cleanup
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (m_frames[i].commandBuffer)
        {
            vkFreeCommandBuffers(m_context->getDevice(),
                m_context->getCommandPool(),
                1,
                &m_frames[i].commandBuffer);
            m_frames[i].commandBuffer = VK_NULL_HANDLE;
        }
        if (m_frames[i].imageAvailableSemaphore)
        {
            vkDestroySemaphore(m_context->getDevice(),
                m_frames[i].imageAvailableSemaphore,
                nullptr);
            m_frames[i].imageAvailableSemaphore = VK_NULL_HANDLE;
        }
        if (m_frames[i].renderFinishedSemaphore)
        {
            vkDestroySemaphore(m_context->getDevice(),
                m_frames[i].renderFinishedSemaphore,
                nullptr);
            m_frames[i].renderFinishedSemaphore = VK_NULL_HANDLE;
        }
        if (m_frames[i].inFlightFence)
        {
            vkDestroyFence(m_context->getDevice(),
                m_frames[i].inFlightFence,
                nullptr);
            m_frames[i].inFlightFence = VK_NULL_HANDLE;
        }
    }

    // Cleanup MVP resources
    if (m_mvpBuffer)
    {
        vkDestroyBuffer(m_context->getDevice(), m_mvpBuffer, nullptr);
        m_mvpBuffer = VK_NULL_HANDLE;
    }
    if (m_mvpMemory)
    {
        vkFreeMemory(m_context->getDevice(), m_mvpMemory, nullptr);
        m_mvpMemory = VK_NULL_HANDLE;
    }
    if (m_mvpDescriptorPool)
    {
        vkDestroyDescriptorPool(m_context->getDevice(), m_mvpDescriptorPool, nullptr);
        m_mvpDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_mvpLayout)
    {
        vkDestroyDescriptorSetLayout(m_context->getDevice(), m_mvpLayout, nullptr);
        m_mvpLayout = VK_NULL_HANDLE;
    }

    // Destroy UIRenderer
    if (m_uiRenderer)
    {
        m_uiRenderer->cleanup();
        delete m_uiRenderer;
        m_uiRenderer = nullptr;
    }

    // Destroy managers
    if (m_rpManager)
    {
        delete m_rpManager;
        m_rpManager = nullptr;
    }
    if (m_pipelineMgr)
    {
        delete m_pipelineMgr;
        m_pipelineMgr = nullptr;
    }
    if (m_resourceMgr)
    {
        delete m_resourceMgr;
        m_resourceMgr = nullptr;
    }

    if (m_swapChain)
    {
        m_swapChain->cleanup();
        delete m_swapChain;
        m_swapChain = nullptr;
    }
}

void Renderer::setTime(Time* time)
{
    m_time = time;
}

void Renderer::enqueueDeferredDestroy(const QueuedChunkDestruction& qcd)
{
    m_deferredFrees[m_currentFrame].push_back(qcd);
}

void Renderer::freeDeferredResources()
{
    auto& list = m_deferredFrees[m_currentFrame];
    if (!list.empty())
    {
        for (auto& info : list)
        {
            if (info.vb != VK_NULL_HANDLE || info.ib != VK_NULL_HANDLE)
            {
                if (m_resourceMgr)
                {
                    m_resourceMgr->destroyChunkBuffers(
                        info.vb, info.vbMem,
                        info.ib, info.ibMem
                    );
                }
            }
        }
        list.clear();
    }
}

// ------------------------------------------------------------------------------
// renderFrame
// ------------------------------------------------------------------------------
void Renderer::renderFrame()
{
    // measure total time spent in renderFrame
    CpuProfiler::ScopedTimer timerFrame("Renderer::renderFrame");

    // 1) update MVP data
    updateMVP();

    // 2) wait for current frame’s fence
    vkWaitForFences(m_context->getDevice(),
        1,
        &m_frames[m_currentFrame].inFlightFence,
        VK_TRUE,
        UINT64_MAX);

    // free resources from last usage of this frame
    freeDeferredResources();

    // 3) reset fence
    vkResetFences(m_context->getDevice(),
        1,
        &m_frames[m_currentFrame].inFlightFence);

    // 4) acquire next swapchain image
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        m_context->getDevice(),
        m_swapChain->getSwapChain(),
        UINT64_MAX,
        m_frames[m_currentFrame].imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapChain();
        return;
    }
    else if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to acquire swapchain image!");
    }

    // 5) reset + begin cmd buffer
    VkCommandBuffer cmdBuf = m_frames[m_currentFrame].commandBuffer;
    vkResetCommandBuffer(cmdBuf, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmdBuf, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin cmd buffer!");
    }

    // 6) begin render pass
    VkClearValue clearVals[2];
    clearVals[0].color = { {0.1f, 0.2f, 0.3f, 1.f} };
    clearVals[1].depthStencil = { 1.f, 0 };

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_rpManager->getRenderPass();
    rpBegin.framebuffer = m_rpManager->getFramebuffers()[imageIndex];
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = m_swapChain->getExtent();
    rpBegin.clearValueCount = 2;
    rpBegin.pClearValues = clearVals;

    vkCmdBeginRenderPass(cmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // 7) bind pipeline
    std::string pipelineName = (m_wireframeOn) ? "voxel_wireframe" : "voxel_fill";
    auto pipelineInfo = m_pipelineMgr->getPipeline(pipelineName);
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineInfo.pipeline);

    // descriptor sets => MVP
    vkCmdBindDescriptorSets(
        cmdBuf,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineInfo.pipelineLayout,
        0, 1, &m_mvpDescriptorSet,
        0, nullptr
    );

    // 8) gather stats
    float dt = (m_time) ? m_time->getDeltaTime() : 0.f;
    float fps = (dt > 0.f) ? (1.f / dt) : 0.f;
    addSample(m_fpsSamples, fps);
    float avgFps = computeAverage(m_fpsSamples);

    float cpuUsage = g_cpuProfiler.GetCpuUsage();
    addSample(m_cpuSamples, cpuUsage);
    float avgCpu = computeAverage(m_cpuSamples);

    // 9) optional frustum cull
    Frustum frustum;
    if (m_enableFrustumCulling)
    {
        frustum = buildCameraFrustum(m_camera, m_swapChain->getExtent());
    }

    // 10) draw voxel chunks
    uint32_t totalVerts = 0;
    uint32_t drawCallCount = 0;

    if (m_voxelWorld)
    {
        const auto& allChunks = m_voxelWorld->getChunkManager().getAllChunks();
        bool useMultiLOD = m_voxelWorld->isUsingMultiLOD();

        for (auto& kv : allChunks)
        {
            Chunk* chunk = kv.second.get();
            if (!chunk) continue;

            if (m_enableFrustumCulling)
            {
                glm::vec3 minB, maxB;
                chunk->getBoundingBox(minB, maxB);
                if (!frustum.intersectsAABB(minB, maxB))
                {
                    continue;
                }
            }

            if (useMultiLOD)
            {
                // naive approach => pick LOD based on distance
                float cx = (chunk->worldX() + 0.5f) * Chunk::SIZE_X;
                float cy = (chunk->worldY() + 0.5f) * Chunk::SIZE_Y;
                float cz = (chunk->worldZ() + 0.5f) * Chunk::SIZE_Z;
                float dist = glm::distance(m_camera.position, glm::vec3(cx, cy, cz));

                int lodIndex = 0;
                if (dist > 500.f)   lodIndex = 1;
                if (dist > 1000.f)   lodIndex = 2;
                if (dist > 2000.f)  lodIndex = 3;
                if (dist > 4000.f)  lodIndex = 4;
                if (dist > 8000.f)  lodIndex = 5;
                if (dist > 16000.f) lodIndex = 6;
                if (dist > 32000.f) {
                    continue;
                } 


                if (lodIndex >= Chunk::MAX_LOD_LEVELS)
                {
                    lodIndex = Chunk::MAX_LOD_LEVELS - 1;
                }

                const auto& cLOD = chunk->getLODData(lodIndex);
                if (cLOD.vertexBuffer == VK_NULL_HANDLE || cLOD.indexCount == 0)
                    continue;

                totalVerts += cLOD.vertexCount;

                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(cmdBuf, 0, 1, &cLOD.vertexBuffer, offsets);
                vkCmdBindIndexBuffer(cmdBuf, cLOD.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                uint32_t idxCount = cLOD.indexCount;
                if (idxCount > 0)
                {
                    vkCmdDrawIndexed(cmdBuf, idxCount, 1, 0, 0, 0);
                    drawCallCount++;
                }
            }
            else
            {
                // single-lod
                if (chunk->getVertexBuffer() == VK_NULL_HANDLE ||
                    chunk->getIndexBuffer() == VK_NULL_HANDLE)
                {
                    continue;
                }

                totalVerts += chunk->getVertexCount();

                VkDeviceSize offsets[] = { 0 };
                auto vb = chunk->getVertexBuffer();
                auto ib = chunk->getIndexBuffer();

                vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vb, offsets);
                vkCmdBindIndexBuffer(cmdBuf, ib, 0, VK_INDEX_TYPE_UINT32);

                uint32_t idxCount = chunk->getIndexCount();
                if (idxCount > 0)
                {
                    vkCmdDrawIndexed(cmdBuf, idxCount, 1, 0, 0, 0);
                    drawCallCount++;
                }
            }
        }
    }

    // 11) UI
    m_uiRenderer->beginFrame();
    m_uiRenderer->renderDebugWindow(
        dt, fps, avgFps,
        cpuUsage, avgCpu,
        totalVerts, drawCallCount,
        m_voxelWorld,
        m_wireframeOn,
        m_enableFrustumCulling,
        m_resourceMgr
    );
    m_uiRenderer->renderImGui(cmdBuf);

    // end render pass
    vkCmdEndRenderPass(cmdBuf);

    // end command buffer
    if (vkEndCommandBuffer(cmdBuf) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to record cmd buffer!");
    }

    // 12) submit
    VkSemaphore waitSemaphores[] = { m_frames[m_currentFrame].imageAvailableSemaphore };
    VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore signalSemaphores[] = { m_frames[m_currentFrame].renderFinishedSemaphore };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = &waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo,
        m_frames[m_currentFrame].inFlightFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit draw cmd buffer!");
    }

    // 13) present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapchains[] = { m_swapChain->getSwapChain() };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_context->getPresentQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapChain();
    }
    else if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to present swapchain image!");
    }

    // 14) next frame
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::setCamera(const Camera& cam)
{
    m_camera = cam;
}

void Renderer::toggleWireframe()
{
    m_wireframeOn = !m_wireframeOn;
}

void Renderer::enableFrustumCulling(bool enable)
{
    m_enableFrustumCulling = enable;
}

// ------------------------------------------------------------------------------
// MVP
// ------------------------------------------------------------------------------
void Renderer::createMVPUniformBuffer()
{
    VkDeviceSize bufferSize = sizeof(MVPBlock);

    createBuffer(bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_mvpBuffer,
        m_mvpMemory);

    // create descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo dpInfo{};
    dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpInfo.maxSets = 1;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_context->getDevice(), &dpInfo, nullptr, &m_mvpDescriptorPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create MVP descriptor pool!");
    }

    // allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_mvpDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_mvpLayout;

    if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo, &m_mvpDescriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate MVP descriptor set!");
    }

    // update descriptor
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = m_mvpBuffer;
    bufInfo.offset = 0;
    bufInfo.range = sizeof(MVPBlock);

    VkWriteDescriptorSet descWrite{};
    descWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrite.dstSet = m_mvpDescriptorSet;
    descWrite.dstBinding = 0;
    descWrite.dstArrayElement = 0;
    descWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descWrite.descriptorCount = 1;
    descWrite.pBufferInfo = &bufInfo;

    vkUpdateDescriptorSets(m_context->getDevice(), 1, &descWrite, 0, nullptr);
}

void Renderer::updateMVP()
{
    glm::mat4 model = glm::mat4(1.f);
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 proj = glm::perspective(
        glm::radians(45.f),
        float(m_swapChain->getExtent().width) / float(m_swapChain->getExtent().height),
        0.1f,
        100000.f
    );
    proj[1][1] *= -1.f; // flip Y

    MVPBlock block{};
    block.mvp = proj * view * model;

    void* data = nullptr;
    vkMapMemory(m_context->getDevice(), m_mvpMemory, 0, sizeof(MVPBlock), 0, &data);
    memcpy(data, &block, sizeof(MVPBlock));
    vkUnmapMemory(m_context->getDevice(), m_mvpMemory);
}

// ------------------------------------------------------------------------------
// createBuffer
// ------------------------------------------------------------------------------
void Renderer::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &bufInfo, nullptr, &buffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_context->getDevice(), buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

    if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(m_context->getDevice(), buffer, bufferMemory, 0);
}

uint32_t Renderer::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((filter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

// ------------------------------------------------------------------------------
// recreateSwapChain
// ------------------------------------------------------------------------------
void Renderer::recreateSwapChain()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window->getGLFWwindow(), &width, &height);
    if (width == 0 || height == 0)
    {
        return; // minimized
    }

    vkDeviceWaitIdle(m_context->getDevice());

    // 1) cleanup old
    m_rpManager->cleanup();
    m_swapChain->cleanup();

    // 2) re-init
    m_swapChain->init(m_context, m_window);

    // 3) re-create pass + framebuffers
    m_rpManager->createRenderPass();
    m_rpManager->createFramebuffers();

    // 4) re-create pipelines
    vkDestroyDescriptorSetLayout(m_context->getDevice(), m_mvpLayout, nullptr);
    m_mvpLayout = VK_NULL_HANDLE;

    auto extent = m_swapChain->getExtent();
    auto renderPass = m_rpManager->getRenderPass();

    m_mvpLayout = m_pipelineMgr->createMVPDescriptorSetLayout();
    m_pipelineMgr->createVoxelPipelineFill("voxel_fill", renderPass, extent, m_mvpLayout);
    m_pipelineMgr->createVoxelPipelineWireframe("voxel_wireframe", renderPass, extent, m_mvpLayout);

    // 5) re-create MVP
    if (m_mvpBuffer)
    {
        vkDestroyBuffer(m_context->getDevice(), m_mvpBuffer, nullptr);
        m_mvpBuffer = VK_NULL_HANDLE;
    }
    if (m_mvpMemory)
    {
        vkFreeMemory(m_context->getDevice(), m_mvpMemory, nullptr);
        m_mvpMemory = VK_NULL_HANDLE;
    }
    if (m_mvpDescriptorPool)
    {
        vkDestroyDescriptorPool(m_context->getDevice(), m_mvpDescriptorPool, nullptr);
        m_mvpDescriptorPool = VK_NULL_HANDLE;
    }

    createMVPUniformBuffer();
}

// ------------------------------------------------------------------------------
// Rolling average
// ------------------------------------------------------------------------------
void Renderer::addSample(std::deque<float>& buffer, float value)
{
    if (buffer.size() >= ROLLING_AVG_SAMPLES)
    {
        buffer.pop_front();
    }
    buffer.push_back(value);
}

float Renderer::computeAverage(const std::deque<float>& buffer)
{
    if (buffer.empty())
        return 0.f;
    float sum = 0.f;
    for (auto val : buffer)
        sum += val;
    return (sum / float(buffer.size()));
}
