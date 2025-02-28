#include "Renderer.h"
#include "Engine/Core/Window.h"         // For m_window->getGLFWwindow()
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Graphics/SwapChain.h"
#include "Engine/Resources/ResourceManager.h"
#include "PipelineManager.h"
#include "RenderPassManager.h"
#include "Engine/Voxels/VoxelWorld.h"
#include "Engine/Voxels/Chunk.h"
#include "Engine/Scene/Camera.h"
#include "Engine/Core/Time.h"
#include "../Utils/CpuProfiler.h"

// Include ImGui + backends
#include "../External Libraries/imgui/imgui.h"
#include "../External Libraries/imgui/backends/imgui_impl_glfw.h"
#include "../External Libraries/imgui/backends/imgui_impl_vulkan.h"

// Include your Frustum utility
#include "Frustum.h"

#include <stdexcept>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <Engine/Utils/Logger.h>
#include <deque>  // needed for addSample, computeAverage usag
#include <Engine/Utils/ThreadPool.h>// needed for addSample, computeAverage usage

// A global CPU profiler used for display in ImGui
static CpuProfiler g_cpuProfiler;

// If you want to reference the global thread pool:
extern ThreadPool g_threadPool;

// -----------------------------------------------------------------------------
// A helper to build the camera frustum from your current camera state
// -----------------------------------------------------------------------------
static Frustum buildCameraFrustum(const Camera& camera, VkExtent2D extent)
{
    float aspect = float(extent.width) / float(extent.height);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    // Flip Y for Vulkan
    proj[1][1] *= -1.f;

    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 vp = proj * view;

    Frustum frustum;
    frustum.extractPlanes(vp);
    return frustum;
}

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
Renderer::Renderer(VulkanContext* context, Window* window, VoxelWorld* voxelWorld)
    : m_context(context)
    , m_window(window)
    , m_voxelWorld(voxelWorld)
{
    // 1) Create SwapChain (pass the Window to init so we can query size)
    m_swapChain = new SwapChain();
    m_swapChain->init(m_context, m_window);

    // 2) Create Managers
    m_resourceMgr = new ResourceManager(m_context);
    m_pipelineMgr = new PipelineManager(m_context, m_resourceMgr);
    m_rpManager = new RenderPassManager(m_context, m_swapChain);

    // 3) Create Render Pass & Framebuffers
    m_rpManager->createRenderPass();
    m_rpManager->createFramebuffers();

    // -----------------------------------------
    // Create an ImGui descriptor pool
    // -----------------------------------------
    {
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 }
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1000 * (uint32_t)(sizeof(poolSizes) / sizeof(poolSizes[0]));
        poolInfo.poolSizeCount = (uint32_t)(sizeof(poolSizes) / sizeof(poolSizes[0]));
        poolInfo.pPoolSizes = poolSizes;

        if (vkCreateDescriptorPool(m_context->getDevice(), &poolInfo, nullptr,
            &m_imguiDescriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create ImGui descriptor pool!");
        }
    }

    // 4) Pipelines
    auto extent = m_swapChain->getExtent();
    auto renderPass = m_rpManager->getRenderPass();

    // Create a descriptor set layout for MVP
    m_mvpLayout = m_pipelineMgr->createMVPDescriptorSetLayout();

    // Create fill + wireframe pipelines
    m_pipelineMgr->createVoxelPipelineFill("voxel_fill", renderPass, extent, m_mvpLayout);
    m_pipelineMgr->createVoxelPipelineWireframe("voxel_wireframe", renderPass, extent, m_mvpLayout);

    // 5) Create MVP Uniform Buffer
    createMVPUniformBuffer();

    // -----------------------------------------
    // Create 2 frames in flight
    // -----------------------------------------
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        // Command buffer
        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.commandPool = m_context->getCommandPool();
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_context->getDevice(), &cmdAlloc,
            &m_frames[i].commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate command buffer for frame " + std::to_string(i));
        }

        // Semaphores
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(m_context->getDevice(), &semInfo, nullptr,
            &m_frames[i].imageAvailableSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(m_context->getDevice(), &semInfo, nullptr,
                &m_frames[i].renderFinishedSemaphore) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create semaphores for frame " + std::to_string(i));
        }

        // Fence (start signaled so we can use it immediately)
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr,
            &m_frames[i].inFlightFence) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create fence for frame " + std::to_string(i));
        }
    }

    // -----------------------------------------
    // Initialize ImGui for Vulkan + GLFW
    // -----------------------------------------
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        // Initialize ImGui for GLFW
        GLFWwindow* glfwWindow = m_window->getGLFWwindow();
        ImGui_ImplGlfw_InitForVulkan(glfwWindow, true);

        // Setup ImGui Vulkan init info
        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = m_context->getInstance();
        init_info.PhysicalDevice = m_context->getPhysicalDevice();
        init_info.Device = m_context->getDevice();
        init_info.QueueFamily = m_context->getGraphicsQueueFamilyIndex();
        init_info.Queue = m_context->getGraphicsQueue();
        init_info.DescriptorPool = m_imguiDescriptorPool;
        init_info.Subpass = 0;
        init_info.MinImageCount = 2;
        init_info.ImageCount = m_swapChain->getImageCount();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.CheckVkResultFn = nullptr;

        ImGui_ImplVulkan_Init(&init_info, renderPass);

        // Upload fonts
        VkCommandPool cmdPool = m_context->getCommandPool();
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuf;
        if (vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo, &cmdBuf) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate command buffer for ImGui font upload!");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmdBuf, &beginInfo);

        ImGui_ImplVulkan_CreateFontsTexture(cmdBuf);

        vkEndCommandBuffer(cmdBuf);

        // Submit & wait
        VkSubmitInfo submitInfo2{};
        submitInfo2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo2.commandBufferCount = 1;
        submitInfo2.pCommandBuffers = &cmdBuf;

        vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo2, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_context->getGraphicsQueue());

        vkFreeCommandBuffers(m_context->getDevice(), cmdPool, 1, &cmdBuf);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
Renderer::~Renderer()
{
    vkDeviceWaitIdle(m_context->getDevice());

    // Clean per-frame resources
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_frames[i].commandBuffer) {
            vkFreeCommandBuffers(m_context->getDevice(),
                m_context->getCommandPool(),
                1,
                &m_frames[i].commandBuffer);
            m_frames[i].commandBuffer = VK_NULL_HANDLE;
        }
        if (m_frames[i].imageAvailableSemaphore) {
            vkDestroySemaphore(m_context->getDevice(),
                m_frames[i].imageAvailableSemaphore,
                nullptr);
            m_frames[i].imageAvailableSemaphore = VK_NULL_HANDLE;
        }
        if (m_frames[i].renderFinishedSemaphore) {
            vkDestroySemaphore(m_context->getDevice(),
                m_frames[i].renderFinishedSemaphore,
                nullptr);
            m_frames[i].renderFinishedSemaphore = VK_NULL_HANDLE;
        }
        if (m_frames[i].inFlightFence) {
            vkDestroyFence(m_context->getDevice(),
                m_frames[i].inFlightFence,
                nullptr);
            m_frames[i].inFlightFence = VK_NULL_HANDLE;
        }
    }

    // Cleanup MVP
    if (m_mvpBuffer) {
        vkDestroyBuffer(m_context->getDevice(), m_mvpBuffer, nullptr);
        m_mvpBuffer = VK_NULL_HANDLE;
    }
    if (m_mvpMemory) {
        vkFreeMemory(m_context->getDevice(), m_mvpMemory, nullptr);
        m_mvpMemory = VK_NULL_HANDLE;
    }
    if (m_mvpDescriptorPool) {
        vkDestroyDescriptorPool(m_context->getDevice(), m_mvpDescriptorPool, nullptr);
        m_mvpDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_mvpLayout) {
        vkDestroyDescriptorSetLayout(m_context->getDevice(), m_mvpLayout, nullptr);
        m_mvpLayout = VK_NULL_HANDLE;
    }

    // ImGui Shutdown
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // Destroy ImGui descriptor pool
    if (m_imguiDescriptorPool) {
        vkDestroyDescriptorPool(m_context->getDevice(), m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }

    // If your Application is the actual owner of m_voxelWorld, remove this line:
    // delete m_voxelWorld;

    delete m_rpManager;
    delete m_pipelineMgr;
    delete m_resourceMgr;

    if (m_swapChain) {
        m_swapChain->cleanup();
        delete m_swapChain;
        m_swapChain = nullptr;
    }
}

// -----------------------------------------------------------------------------
// createMVPUniformBuffer
// -----------------------------------------------------------------------------
void Renderer::createMVPUniformBuffer()
{
    VkDeviceSize bufferSize = sizeof(MVPBlock);

    createBuffer(bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_mvpBuffer,
        m_mvpMemory);

    // Create descriptor pool for exactly 1 UBO
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_context->getDevice(), &poolInfo, nullptr, &m_mvpDescriptorPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor pool for MVP!");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_mvpDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_mvpLayout;

    if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo, &m_mvpDescriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate descriptor set for MVP!");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_mvpBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(MVPBlock);

    VkWriteDescriptorSet descWrite{};
    descWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrite.dstSet = m_mvpDescriptorSet;
    descWrite.dstBinding = 0;
    descWrite.dstArrayElement = 0;
    descWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descWrite.descriptorCount = 1;
    descWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_context->getDevice(), 1, &descWrite, 0, nullptr);
}

// -----------------------------------------------------------------------------
// setCamera
// -----------------------------------------------------------------------------
void Renderer::setCamera(const Camera& cam)
{
    m_camera = cam;
}

// -----------------------------------------------------------------------------
// updateMVP
// -----------------------------------------------------------------------------
void Renderer::updateMVP()
{
    glm::mat4 model = glm::mat4(1.f);
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 proj = glm::perspective(glm::radians(45.f),
        float(m_swapChain->getExtent().width) /
        float(m_swapChain->getExtent().height),
        0.1f, 100.f);
    proj[1][1] *= -1.f; // Flip Y for Vulkan

    MVPBlock block{};
    block.mvp = proj * view * model;

    void* data;
    vkMapMemory(m_context->getDevice(), m_mvpMemory, 0, sizeof(MVPBlock), 0, &data);
    memcpy(data, &block, sizeof(MVPBlock));
    vkUnmapMemory(m_context->getDevice(), m_mvpMemory);
}

// -----------------------------------------------------------------------------
// toggleWireframe
// -----------------------------------------------------------------------------
void Renderer::toggleWireframe()
{
    m_wireframeOn = !m_wireframeOn;
}

// -----------------------------------------------------------------------------
// Rolling-average helpers
// -----------------------------------------------------------------------------
void Renderer::addSample(std::deque<float>& buffer, float value)
{
    if (buffer.size() >= ROLLING_AVG_SAMPLES) {
        buffer.pop_front();
    }
    buffer.push_back(value);
}

float Renderer::computeAverage(const std::deque<float>& buffer)
{
    if (buffer.empty()) return 0.0f;
    float sum = 0.f;
    for (float val : buffer) {
        sum += val;
    }
    return sum / static_cast<float>(buffer.size());
}

// -----------------------------------------------------------------------------
// renderFrame
// -----------------------------------------------------------------------------
void Renderer::renderFrame()
{
    // 1) Wait for this frame’s fence => ensure GPU is done with last usage
    vkWaitForFences(m_context->getDevice(),
        1,
        &m_frames[m_currentFrame].inFlightFence,
        VK_TRUE,
        UINT64_MAX);

    // 2) Reset fence so we can use it again
    vkResetFences(m_context->getDevice(),
        1,
        &m_frames[m_currentFrame].inFlightFence);

    // 3) Update MVP
    updateMVP();

    // (Optional) Build the frustum if culling is on
    Frustum frustum;
    if (m_enableFrustumCulling) {
        frustum = buildCameraFrustum(m_camera, m_swapChain->getExtent());
    }

    // 4) Acquire swapchain image
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        m_context->getDevice(),
        m_swapChain->getSwapChain(),
        UINT64_MAX,
        m_frames[m_currentFrame].imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapChain();
        return;
    }
    else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to acquire swapchain image!");
    }

    // 5) Reset command buffer
    VkCommandBuffer cmdBuf = m_frames[m_currentFrame].commandBuffer;
    vkResetCommandBuffer(cmdBuf, 0);

    // 6) Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmdBuf, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }

    // 7) Render pass
    VkClearValue clearVals[2];
    clearVals[0].color = { { 0.1f, 0.2f, 0.3f, 1.f } };
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

    // 8) Pipeline + descriptor sets
    std::string pipelineName = (m_wireframeOn) ? "voxel_wireframe" : "voxel_fill";
    PipelineInfo pipelineInfo = m_pipelineMgr->getPipeline(pipelineName);

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineInfo.pipeline);
    vkCmdBindDescriptorSets(cmdBuf,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineInfo.pipelineLayout,
        0, 1, &m_mvpDescriptorSet,
        0, nullptr);

    // 9) Draw chunks from m_voxelWorld
    uint32_t totalVertices = 0;
    uint32_t drawCallCount = 0;
    float dt = (m_time) ? m_time->getDeltaTime() : 0.f;
    float fps = (dt > 0.f) ? 1.f / dt : 0.f;

    // We'll push the fps sample, then compute average if we want it:
    addSample(m_fpsSamples, fps);
    float avgFps = computeAverage(m_fpsSamples);

    // CPU usage
    float cpuUsage = g_cpuProfiler.GetCpuUsage();
    addSample(m_cpuSamples, cpuUsage);
    float avgCpu = computeAverage(m_cpuSamples);

    // If there's a voxel world, draw it:
    if (m_voxelWorld) {
        const auto& allChunks = m_voxelWorld->getChunkManager().getAllChunks();
        for (auto& kv : allChunks) {
            Chunk* chunk = kv.second.get();
            if (!chunk) continue;

            if (chunk->getVertexBuffer() == VK_NULL_HANDLE ||
                chunk->getIndexBuffer() == VK_NULL_HANDLE)
            {
                continue;
            }

            // Frustum cull if enabled
            if (m_enableFrustumCulling) {
                glm::vec3 minB, maxB;
                chunk->getBoundingBox(minB, maxB);
                if (!frustum.intersectsAABB(minB, maxB)) {
                    continue;
                }
            }

            totalVertices += chunk->getVertexCount();

            VkDeviceSize offsets[] = { 0 };
            VkBuffer vb = chunk->getVertexBuffer();
            vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vb, offsets);
            vkCmdBindIndexBuffer(cmdBuf, chunk->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            uint32_t idxCount = chunk->getIndexCount();
            if (idxCount > 0) {
                vkCmdDrawIndexed(cmdBuf, idxCount, 1, 0, 0, 0);
                drawCallCount++;
            }
        }
    }

    // 10) ImGui
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Debug");
    ImGui::Text("Wireframe: %s", m_wireframeOn ? "ON" : "OFF");
    ImGui::Checkbox("Frustum Culling", &m_enableFrustumCulling);
    ImGui::Separator();
    ImGui::Text("Delta Time:  %.3f s", dt);
    ImGui::Text("FPS (Instant):  %.2f", fps);
    ImGui::Text("FPS (Average):  %.2f", avgFps);
    ImGui::Text("CPU Usage (Instant):  %.1f%%", cpuUsage);
    ImGui::Text("CPU Usage (Average):  %.1f%%", avgCpu);
    ImGui::Separator();
    ImGui::Text("Vertex Count:  %u", totalVertices);
    ImGui::Text("Draw Calls:    %u", drawCallCount);

    // If there's a voxel world, show chunk stats
    if (m_voxelWorld) {
        auto& chunkMgr = m_voxelWorld->getChunkManager();
        ImGui::Text("Chunk Count:  %zu", chunkMgr.getAllChunks().size());

        // If you also track total voxel usage:
        auto usage = chunkMgr.getTotalVoxelUsage();
        ImGui::Text("Active Voxels: %zu", usage.first);
        ImGui::Text("Empty Voxels:  %zu", usage.second);
    }

    // Show the ImGui panel
    ImGui::End();

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);

    // End render pass
    vkCmdEndRenderPass(cmdBuf);

    // End command buffer
    if (vkEndCommandBuffer(cmdBuf) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }

    // 11) Submit
    VkSemaphore waitSemaphores[] = { m_frames[m_currentFrame].imageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { m_frames[m_currentFrame].renderFinishedSemaphore };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo,
        m_frames[m_currentFrame].inFlightFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }

    // 12) Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapchains[] = { m_swapChain->getSwapChain() };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_context->getPresentQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapChain();
    }
    else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image!");
    }

    // 13) Advance frame index
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// -----------------------------------------------------------------------------
// createBuffer & findMemoryType
// -----------------------------------------------------------------------------
void Renderer::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory
)
{
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_context->getDevice(), &bufInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(m_context->getDevice(), buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, properties);

    if (vkAllocateMemory(m_context->getDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(m_context->getDevice(), buffer, bufferMemory, 0);
}

uint32_t Renderer::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((filter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

// -----------------------------------------------------------------------------
// recreateSwapChain
// -----------------------------------------------------------------------------
void Renderer::recreateSwapChain()
{
    // If window is minimized (width=0 or height=0), just pause
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window->getGLFWwindow(), &width, &height);
    if (width == 0 || height == 0) {
        return;
    }

    vkDeviceWaitIdle(m_context->getDevice());

    // 1) Cleanup old framebuffers/render pass
    m_rpManager->cleanup();

    // 2) Cleanup old swapchain
    m_swapChain->cleanup();

    // 3) Re-init the swapchain with updated size
    m_swapChain->init(m_context, m_window);

    // 4) Recreate the render pass + framebuffers
    m_rpManager->createRenderPass();
    m_rpManager->createFramebuffers();

    // 5) Recreate pipelines
    vkDestroyDescriptorSetLayout(m_context->getDevice(), m_mvpLayout, nullptr);
    m_mvpLayout = VK_NULL_HANDLE;

    auto extent = m_swapChain->getExtent();
    auto renderPass = m_rpManager->getRenderPass();

    m_mvpLayout = m_pipelineMgr->createMVPDescriptorSetLayout();
    m_pipelineMgr->createVoxelPipelineFill("voxel_fill", renderPass, extent, m_mvpLayout);
    m_pipelineMgr->createVoxelPipelineWireframe("voxel_wireframe", renderPass, extent, m_mvpLayout);

    // 6) Recreate the MVP uniform buffer + descriptor set
    if (m_mvpBuffer) {
        vkDestroyBuffer(m_context->getDevice(), m_mvpBuffer, nullptr);
        m_mvpBuffer = VK_NULL_HANDLE;
    }
    if (m_mvpMemory) {
        vkFreeMemory(m_context->getDevice(), m_mvpMemory, nullptr);
        m_mvpMemory = VK_NULL_HANDLE;
    }
    if (m_mvpDescriptorPool) {
        vkDestroyDescriptorPool(m_context->getDevice(), m_mvpDescriptorPool, nullptr);
        m_mvpDescriptorPool = VK_NULL_HANDLE;
    }

    createMVPUniformBuffer();

    // 7) If needed, re-init ImGui for the new swapchain size
    ImGui_ImplVulkan_SetMinImageCount(2);

    // If your ImGui version has no ImGui_ImplVulkanH_CreateOrResizeWindow, comment it out:
    // ImGui_ImplVulkanH_CreateOrResizeWindow(...);

    // Next frame will proceed with the updated swapchain
}
