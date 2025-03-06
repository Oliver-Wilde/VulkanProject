#include "UIRenderer.h"

#include "../External Libraries/imgui/imgui.h"
#include "../External Libraries/imgui/backends/imgui_impl_glfw.h"
#include "../External Libraries/imgui/backends/imgui_impl_vulkan.h"

#include <stdexcept>

UIRenderer::UIRenderer()
{
}

UIRenderer::~UIRenderer()
{
}

void UIRenderer::init(VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    uint32_t queueFamily,
    VkQueue queue,
    VkDescriptorPool descriptorPool,
    VkRenderPass renderPass,
    uint32_t imageCount,
    GLFWwindow* window,
    VkCommandPool cmdPool)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    if (!window)
        throw std::runtime_error("Invalid GLFW window passed to UIRenderer::init");

    // Initialize GLFW backend with a valid window.
    ImGui_ImplGlfw_InitForVulkan(window, true);

    // Setup Vulkan initialization info.
    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = queueFamily;
    init_info.Queue = queue;
    init_info.DescriptorPool = descriptorPool;
    init_info.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = imageCount;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = nullptr;

    // Call ImGui_ImplVulkan_Init (this function returns void in the official backend)
    ImGui_ImplVulkan_Init(&init_info, renderPass);

    // ---- Font Upload ----
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    if (vkAllocateCommandBuffers(device, &allocInfo, &cmdBuf) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate command buffer for font upload!");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmdBuf, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin command buffer for font upload!");

    ImGui_ImplVulkan_CreateFontsTexture(cmdBuf);
    vkEndCommandBuffer(cmdBuf);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    if (vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit font upload command buffer!");

    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void UIRenderer::update()
{
    // Build your UI here.
    ImGui::Begin("Debug");
    ImGui::Text("Wireframe: %s", "OFF"); // Replace with dynamic value if needed.
    static bool frustumCulling = false;
    ImGui::Checkbox("Frustum Culling", &frustumCulling); // Replace with dynamic pointer if needed.
    ImGui::Separator();

    static int mesherTypeIndex = 0;
    const char* mesherTypes[] = { "Greedy", "Naive" };
    if (ImGui::Combo("Mesher Type", &mesherTypeIndex, mesherTypes, IM_ARRAYSIZE(mesherTypes)))
    {
        // Callback handling to update mesher type should be done externally.
    }
    ImGui::Separator();

    ImGui::Text("Delta Time:  %.3f s", 0.016f);
    ImGui::Text("FPS (Instant):  %.2f", 60.f);
    ImGui::Text("FPS (Average):  %.2f", 60.f);
    ImGui::Text("CPU Usage (Instant):  %.1f%%", 20.f);
    ImGui::Text("CPU Usage (Average):  %.1f%%", 20.f);
    ImGui::Separator();
    ImGui::Text("Vertex Count:  %u", 1000);
    ImGui::Text("Draw Calls:    %u", 50);
    ImGui::End();
}

void UIRenderer::render(VkCommandBuffer cmdBuf)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    update();

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
}

void UIRenderer::shutdown()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}
