#include "UIRenderer.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Core/Window.h"
#include "Engine/Voxels/VoxelWorld.h"
#include "Engine/Voxels/ChunkManager.h"

// We MUST include ResourceManager to call its methods
#include "Engine/Resources/ResourceManager.h"

// Needed so we can read s_totalCPUBytes
#include "Engine/Voxels/Chunk.h"

// ImGui + Vulkan/GLFW backends
#include "../External Libraries/imgui/imgui.h"
#include "../External Libraries/imgui/backends/imgui_impl_glfw.h"
#include "../External Libraries/imgui/backends/imgui_impl_vulkan.h"

#include <stdexcept>
#include <vector>
#include <Engine/Utils/CpuProfiler.h>

UIRenderer::UIRenderer()
{
}

UIRenderer::~UIRenderer()
{
    cleanup();
}

void UIRenderer::init(VulkanContext* context, Window* window, VkRenderPass renderPass, uint32_t imageCount)
{
    if (!context || !window) {
        throw std::runtime_error("UIRenderer::init - context or window is null!");
    }

    m_context = context;

    // 1) Create a descriptor pool for ImGui
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

    if (vkCreateDescriptorPool(m_context->getDevice(), &poolInfo, nullptr, &m_imguiPool) != VK_SUCCESS)
    {
        throw std::runtime_error("UIRenderer: Failed to create ImGui descriptor pool!");
    }

    // 2) Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io; // If you want to silence the compiler about unused 'io'

    GLFWwindow* glfwWindow = window->getGLFWwindow();
    if (!glfwWindow) {
        throw std::runtime_error("UIRenderer::init - No valid GLFW window!");
    }
    ImGui_ImplGlfw_InitForVulkan(glfwWindow, /*install_callbacks=*/true);

    // Setup ImGui Vulkan init info
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = m_context->getInstance();
    initInfo.PhysicalDevice = m_context->getPhysicalDevice();
    initInfo.Device = m_context->getDevice();
    initInfo.QueueFamily = m_context->getGraphicsQueueFamilyIndex();
    initInfo.Queue = m_context->getGraphicsQueue();
    initInfo.DescriptorPool = m_imguiPool;
    initInfo.Subpass = 0;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = nullptr;

    ImGui_ImplVulkan_Init(&initInfo, renderPass);

    // 3) Upload fonts
    VkCommandPool cmdPool = m_context->getCommandPool();
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    if (vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo, &cmdBuf) != VK_SUCCESS) {
        throw std::runtime_error("UIRenderer::init - Failed to allocate command buffer for ImGui font upload!");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    ImGui_ImplVulkan_CreateFontsTexture(cmdBuf);

    vkEndCommandBuffer(cmdBuf);

    // Submit & wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_context->getGraphicsQueue());

    vkFreeCommandBuffers(m_context->getDevice(), cmdPool, 1, &cmdBuf);
    ImGui_ImplVulkan_DestroyFontUploadObjects();

    m_initialized = true;
}

void UIRenderer::cleanup()
{
    // Must be a member function => can safely check m_initialized
    if (!m_initialized) {
        return;
    }

    vkDeviceWaitIdle(m_context->getDevice());

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_imguiPool) {
        vkDestroyDescriptorPool(m_context->getDevice(), m_imguiPool, nullptr);
        m_imguiPool = VK_NULL_HANDLE;
    }

    m_initialized = false;
}

void UIRenderer::beginFrame()
{
    if (!m_initialized) return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void UIRenderer::renderImGui(VkCommandBuffer cmdBuf)
{
    if (!m_initialized) return;

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
}

// -----------------------------------------------------------------------------
// renderDebugWindow
// -----------------------------------------------------------------------------
void UIRenderer::renderDebugWindow(
    float dt, float fps, float avgFps,
    float cpu, float avgCpu,
    uint32_t verts, uint32_t draws,
    VoxelWorld* world,
    bool& wireframe,
    bool& frustum,
    ResourceManager* rm)
{
    if (!m_initialized) return;

    ImGui::Begin("Debug");

    /* toggles */
    if (ImGui::Button(wireframe ? "Wireframe ON" : "Wireframe OFF"))
        wireframe = !wireframe;
    ImGui::SameLine();
    ImGui::Checkbox("Frustum culling", &frustum);
    ImGui::Separator();

    /* mesher type */
    int m = int(world->getMesherType());
    if (ImGui::RadioButton("Greedy mesher", m == 0)) m = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("Naive mesher", m == 1)) m = 1;
    world->setMesherType(static_cast<VoxelWorld::MesherType>(m));
    ImGui::Separator();

    /* perf */
    ImGui::Text("Δt:  %.3f ms", dt * 1000.f);
    ImGui::Text("FPS: %.1f (avg %.1f)", fps, avgFps);
    ImGui::Text("CPU: %.1f%% (avg %.1f%%)", cpu, avgCpu);
    ImGui::Separator();

    /* scene stats */
    ImGui::Text("Draw calls: %u", draws);
    ImGui::Text("Vertices:   %u", verts);

    /* chunk counts */
    auto& cm = world->getChunkManager();
    ImGui::Text("Chunks: %zu", cm.getAllChunks().size());

    /* CPU mem */
    double cpuMB = Chunk::s_totalCPUBytes.load(std::memory_order_relaxed) /
        (1024.0 * 1024.0);
    ImGui::Text("Chunk CPU mem: %.2f MB", cpuMB);

    /* GPU mem */
    if (rm)
    {
        double gpuMB = rm->GetTotalGPUBufferBytes() / (1024.0 * 1024.0);
        ImGui::Text("GPU buffer mem: %.2f MB", gpuMB);
    }

    /*───────────────────────────────────────────────────────────────────
      NEW  –– mesh‑upload budget & queue stats
     ──────────────────────────────────────────────────────────────────*/
    if (ImGui::CollapsingHeader("Chunk upload budget", ImGuiTreeNodeFlags_DefaultOpen))
    {
        size_t bytesBudget = world->getUploadBudgetBytes();
        int    chunkBudget = world->getUploadBudgetChunks();

        float mbBudget = float(bytesBudget) / (1024.0f * 1024.0f);
        if (ImGui::SliderFloat("MB / frame", &mbBudget, 0.5f, 16.0f, "%.1f"))
        {
            world->setUploadBudget(size_t(mbBudget * 1024.0f * 1024.0f),
                chunkBudget);
            bytesBudget = world->getUploadBudgetBytes();
        }
        if (ImGui::SliderInt("Chunks / frame", &chunkBudget, 1, 32))
        {
            world->setUploadBudget(bytesBudget, chunkBudget);
        }
        ImGui::Text("Pending uploads: %zu", world->getPendingUploadCount());
    }

    /* CPU profiler table */
    if (ImGui::CollapsingHeader("CPU timers"))
    {
        const auto& map = CpuProfiler::GetProfileRecords();
        for (auto& kv : map)
        {
            const auto& rec = kv.second;
            double avg = rec.callCount ? rec.accumTimeMs / rec.callCount : 0.0;
            ImGui::Text("%s  |  last %.2f ms / avg %.2f ms (%d calls)",
                kv.first.c_str(), rec.lastTimeMs, avg, rec.callCount);
        }
    }

    ImGui::End();
}