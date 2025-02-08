#include "Renderer.h"
#include "Engine/Core/Window.h"          // So we can use m_window->getGLFWwindow()
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Graphics/SwapChain.h"
#include "Engine/Resources/ResourceManager.h"
#include "PipelineManager.h"
#include "RenderPassManager.h"
#include "Engine/Voxels/VoxelWorld.h"
#include "Engine/Voxels/Chunk.h"
#include "Engine/Scene/Camera.h"

#include <stdexcept>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Engine/Core/Time.h" 


// Include ImGui + backends
#include "../External Libraries/imgui/imgui.h"
#include "../External Libraries/imgui/backends/imgui_impl_glfw.h"
#include "../External Libraries/imgui/backends/imgui_impl_vulkan.h"

// -----------------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------------
Renderer::Renderer(VulkanContext* context, Window* window)
    : m_context(context), m_window(window)
{
    // 1) Create SwapChain
    m_swapChain = new SwapChain();
    m_swapChain->init(m_context);

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

        if (vkCreateDescriptorPool(m_context->getDevice(), &poolInfo, nullptr, &m_imguiDescriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create ImGui descriptor pool!");
        }
    }

    // 4) Pipelines
    auto extent = m_swapChain->getExtent();
    auto renderPass = m_rpManager->getRenderPass();

    m_mvpLayout = m_pipelineMgr->createMVPDescriptorSetLayout();
    m_pipelineMgr->createVoxelPipelineFill("voxel_fill", renderPass, extent, m_mvpLayout);
    m_pipelineMgr->createVoxelPipelineWireframe("voxel_wireframe", renderPass, extent, m_mvpLayout);

    // 5) Create VoxelWorld
    m_voxelWorld = new VoxelWorld(m_context);
    m_voxelWorld->initWorld();

    // 6) Create MVP Uniform Buffer
    createMVPUniformBuffer();

    // 7) Semaphores
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(m_context->getDevice(), &semInfo, nullptr, &m_imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_context->getDevice(), &semInfo, nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create semaphores!");
    }

    // -----------------------------------------
    // Initialize ImGui for Vulkan + GLFW
    // -----------------------------------------
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // optional

        // 1) Initialize ImGui for GLFW
        GLFWwindow* glfwWindow = m_window->getGLFWwindow();
        ImGui_ImplGlfw_InitForVulkan(glfwWindow, true);

        // 2) Setup ImGui Vulkan init info
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_context->getInstance();
        init_info.PhysicalDevice = m_context->getPhysicalDevice();
        init_info.Device = m_context->getDevice();
        init_info.QueueFamily = m_context->getGraphicsQueueFamilyIndex();
        init_info.Queue = m_context->getGraphicsQueue();
        init_info.DescriptorPool = m_imguiDescriptorPool;
        init_info.Subpass = 0; // if using the same subpass
        init_info.MinImageCount = 2; // match your swapchain
        init_info.ImageCount = m_swapChain->getImageCount();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.CheckVkResultFn = nullptr;

        ImGui_ImplVulkan_Init(&init_info, renderPass);

        // 3) Upload fonts with one-time command buffer
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

        // Free temp command buffer
        vkFreeCommandBuffers(m_context->getDevice(), cmdPool, 1, &cmdBuf);

        // Cleanup font staging
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }
}

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
Renderer::~Renderer()
{
    vkDeviceWaitIdle(m_context->getDevice());

    // Destroy semaphores
    if (m_imageAvailableSemaphore) {
        vkDestroySemaphore(m_context->getDevice(), m_imageAvailableSemaphore, nullptr);
        m_imageAvailableSemaphore = VK_NULL_HANDLE;
    }
    if (m_renderFinishedSemaphore) {
        vkDestroySemaphore(m_context->getDevice(), m_renderFinishedSemaphore, nullptr);
        m_renderFinishedSemaphore = VK_NULL_HANDLE;
    }

    // Cleanup MVP
    if (m_mvpBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_context->getDevice(), m_mvpBuffer, nullptr);
        m_mvpBuffer = VK_NULL_HANDLE;
    }
    if (m_mvpMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_context->getDevice(), m_mvpMemory, nullptr);
        m_mvpMemory = VK_NULL_HANDLE;
    }
    if (m_mvpDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_context->getDevice(), m_mvpDescriptorPool, nullptr);
        m_mvpDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_mvpLayout != VK_NULL_HANDLE) {
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

    delete m_voxelWorld;
    delete m_rpManager;
    delete m_pipelineMgr;
    delete m_resourceMgr;

    // Destroy swapchain
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

    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_mvpBuffer,
        m_mvpMemory
    );

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

    glm::mat4 proj = glm::perspective(
        glm::radians(45.f),
        (float)m_swapChain->getExtent().width / (float)m_swapChain->getExtent().height,
        0.1f,
        100.f
    );

    // Flip Y for Vulkan
    proj[1][1] *= -1.f;

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
// renderFrame
// -----------------------------------------------------------------------------
void Renderer::renderFrame()
{
    // --- 0) Gather timing
    float dt = 0.0f;
    if (m_time) {
        dt = m_time->getDeltaTime();
    }
    float fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;

    // We'll track how many draw calls this frame
    uint32_t drawCallCount = 0;
    // We'll also track total vertices here:
    uint32_t totalVertices = 0;

    // 1) Update MVP
    updateMVP();

    // 2) Acquire swapchain image
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        m_context->getDevice(),
        m_swapChain->getSwapChain(),
        UINT64_MAX,
        m_imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Handle window resize if needed
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swapchain image!");
    }

    // 3) Allocate a command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_context->getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    if (vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo, &cmdBuf) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffer!");
    }

    // 4) Begin recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    // 5) Render pass setup
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

    // 6) Choose pipeline + bind
    std::string pipelineName = (m_wireframeOn) ? "voxel_wireframe" : "voxel_fill";
    PipelineInfo pipelineInfo = m_pipelineMgr->getPipeline(pipelineName);

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineInfo.pipeline);
    vkCmdBindDescriptorSets(
        cmdBuf,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineInfo.pipelineLayout,
        0, 1,
        &m_mvpDescriptorSet,
        0, nullptr
    );

    // 7) Draw chunk meshes (accumulate total vertices & draw calls)
    const auto& allChunks = m_voxelWorld->getChunkManager().getAllChunks();
    for (auto& kv : allChunks) {
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;

        // Check if chunk has valid GPU buffers
        if (chunk->getVertexBuffer() == VK_NULL_HANDLE ||
            chunk->getIndexBuffer() == VK_NULL_HANDLE)
        {
            continue;
        }

        // Accumulate this chunk's vertex count
        totalVertices += chunk->getVertexCount();

        // Bind vertex + index
        VkDeviceSize offsets[] = { 0 };
        VkBuffer vb = chunk->getVertexBuffer();
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vb, offsets);
        vkCmdBindIndexBuffer(cmdBuf, chunk->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // Draw if indices
        uint32_t idxCount = chunk->getIndexCount();
        if (idxCount > 0) {
            vkCmdDrawIndexed(cmdBuf, idxCount, 1, 0, 0, 0);
            drawCallCount++;
        }
    }

    // 8) ImGui overlay
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Performance metrics UI
    ImGui::Begin("Debug");
    ImGui::Text("Wireframe is %s", m_wireframeOn ? "ON" : "OFF");
    ImGui::Separator();
    ImGui::Text("Delta Time:  %.3f s", dt);
    ImGui::Text("FPS:         %.2f", fps);
    ImGui::Text("Vertex Count:%u", totalVertices);
    ImGui::Text("Draw Calls:  %u", drawCallCount);
    ImGui::Text("Chunk Count: %zu", allChunks.size());
    ImGui::End();

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);

    // end render pass
    vkCmdEndRenderPass(cmdBuf);

    // end command buffer
    if (vkEndCommandBuffer(cmdBuf) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }

    // 9) Submit
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submitInfo2{};
    submitInfo2.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo2.waitSemaphoreCount = 1;
    submitInfo2.pWaitSemaphores = &m_imageAvailableSemaphore;
    submitInfo2.pWaitDstStageMask = waitStages;
    submitInfo2.commandBufferCount = 1;
    submitInfo2.pCommandBuffers = &cmdBuf;
    submitInfo2.signalSemaphoreCount = 1;
    submitInfo2.pSignalSemaphores = &m_renderFinishedSemaphore;

    if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo2, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }

    // 10) Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    VkSwapchainKHR swcs[] = { m_swapChain->getSwapChain() };
    presentInfo.pSwapchains = swcs;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_context->getPresentQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // handle swapchain recreation if needed
    }
    else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image!");
    }

    // free the command buffer
    vkFreeCommandBuffers(m_context->getDevice(), m_context->getCommandPool(), 1, &cmdBuf);
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
        if ((filter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}
