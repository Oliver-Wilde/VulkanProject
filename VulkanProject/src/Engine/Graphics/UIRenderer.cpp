#include "UIRenderer.h"
#include "Engine/Graphics/VulkanContext.h"
#include "Engine/Core/Window.h"
#include "Engine/Voxels/VoxelWorld.h"
#include "Engine/Voxels/ChunkManager.h"

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

    // -----------------------------------------------------------
    // 1) Create a descriptor pool for ImGui
    // -----------------------------------------------------------
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

    // -----------------------------------------------------------
    // 2) Initialize ImGui context and bind to GLFW / Vulkan
    // -----------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io; // If you want to silence the compiler about unused 'io'

    // Initialize ImGui for GLFW
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

    // Initialize ImGui for Vulkan
    ImGui_ImplVulkan_Init(&initInfo, renderPass);

    // -----------------------------------------------------------
    // 3) Upload ImGui fonts
    // -----------------------------------------------------------
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
    if (!m_initialized) {
        return; // already cleaned or never init
    }

    vkDeviceWaitIdle(m_context->getDevice());

    // Shut down ImGui
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // Destroy descriptor pool
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
    float dt,
    float fps,
    float avgFps,
    float cpuUsage,
    float avgCpu,
    uint32_t totalVertices,
    uint32_t drawCallCount,
    VoxelWorld* voxelWorld,
    bool& wireframeOn,
    bool& enableFrustumCulling
)
{
    if (!m_initialized) {
        return;
    }

    ImGui::Begin("Debug");

    // Show wireframe status and button to toggle
    ImGui::Text("Wireframe: %s", wireframeOn ? "ON" : "OFF");
    if (ImGui::Button("Toggle Wireframe")) {
        wireframeOn = !wireframeOn;
    }

    // Frustum culling
    ImGui::Checkbox("Frustum Culling", &enableFrustumCulling);
    ImGui::Separator();

    // Mesher selection
    static int mesherTypeIndex = 0; // 0 = GREEDY, 1 = NAIVE
    const char* mesherTypes[] = { "Greedy", "Naive" };
    if (ImGui::Combo("Mesher Type", &mesherTypeIndex, mesherTypes, IM_ARRAYSIZE(mesherTypes))) {
        // Make sure voxelWorld is valid before calling setMesherType.
        if (voxelWorld) {
            if (mesherTypeIndex == 0) {
                voxelWorld->setMesherType(VoxelWorld::MesherType::GREEDY);
            }
            else {
                voxelWorld->setMesherType(VoxelWorld::MesherType::NAIVE);
            }
        }
    }
    ImGui::Separator();

    // Show frame time, FPS, CPU usage
    ImGui::Text("Delta Time:  %.3f s", dt);
    ImGui::Text("FPS (Instant):  %.2f", fps);
    ImGui::Text("FPS (Average):  %.2f", avgFps);
    ImGui::Text("CPU Usage (Instant):  %.1f%%", cpuUsage);
    ImGui::Text("CPU Usage (Average):  %.1f%%", avgCpu);
    ImGui::Separator();

    // Show total geometry
    ImGui::Text("Vertex Count:  %u", totalVertices);
    ImGui::Text("Draw Calls:    %u", drawCallCount);

    // -------------------------------------------------------------------------
    // Check if voxelWorld is null
    // -------------------------------------------------------------------------
    if (!voxelWorld) {
        ImGui::Text("No VoxelWorld pointer (null).");
        ImGui::End();
        return;
    }

    auto& chunkMgr = voxelWorld->getChunkManager();

    // -------------------------------------------------------------------------
    // Display chunk count, total usage (active/empty)
    // -------------------------------------------------------------------------
    ImGui::Text("Chunk Count:  %zu", chunkMgr.getAllChunks().size());
    auto usage = chunkMgr.getTotalVoxelUsage();
    ImGui::Text("Active Voxels: %zu", usage.first);
    ImGui::Text("Empty Voxels:  %zu", usage.second);

    // -------------------------------------------------------------------------
    // Additional Debug: Count how many chunks are EMPTY, SOLID, or NORMAL
    // -------------------------------------------------------------------------
    {
        size_t emptyChunkCount = 0;
        size_t solidChunkCount = 0;
        size_t normalChunkCount = 0;

        // Also track total active/empty across all chunks for cross-check
        size_t totalActiveVoxels = 0;
        size_t totalEmptyVoxels = 0;

        const auto& allChunks = chunkMgr.getAllChunks();
        for (auto& kv : allChunks)
        {
            Chunk* c = kv.second.get();
            if (!c) continue;

            // Check chunk state
            switch (c->getState())
            {
            case Chunk::ChunkState::EMPTY:
                emptyChunkCount++;
                break;
            case Chunk::ChunkState::SOLID:
                solidChunkCount++;
                break;
            case Chunk::ChunkState::NORMAL:
                normalChunkCount++;
                break;
            }

            // For comparison, accumulate usage
            auto cUsage = c->getVoxelUsage();
            totalActiveVoxels += cUsage.first;
            totalEmptyVoxels += cUsage.second;
        }

        ImGui::Separator();
        ImGui::Text("Chunk States:");
        ImGui::Text("  EMPTY:  %zu", emptyChunkCount);
        ImGui::Text("  SOLID:  %zu", solidChunkCount);
        ImGui::Text("  NORMAL: %zu", normalChunkCount);

        // These might differ from chunkMgr.getTotalVoxelUsage() if you're storing
        // partial data or if the code lumps them differently. But it's a good cross-check:
        ImGui::Text("Recounted Active Voxels: %zu", totalActiveVoxels);
        ImGui::Text("Recounted Empty Voxels:  %zu", totalEmptyVoxels);
    }

    ImGui::Separator();
    ImGui::Text("CPU Timing (ms):");
    const auto& profileMap = CpuProfiler::GetProfileRecords();
    for (auto& kv : profileMap)
    {
        const std::string& label = kv.first;
        const ProfileRecord& rec = kv.second;

        double average = (rec.callCount > 0)
            ? (rec.accumTimeMs / double(rec.callCount))
            : 0.0;

        ImGui::Text("%s: Last=%.2fms, Avg=%.2fms (%d calls)",
            label.c_str(),
            rec.lastTimeMs,
            average,
            rec.callCount);
    }

    ImGui::End();
}
