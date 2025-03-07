#pragma once

#include <vulkan/vulkan.h>

// Forward declarations to avoid heavy includes
class VulkanContext;
class Window;
class VoxelWorld;

/**
 * UIRenderer is responsible for all ImGui-based UI:
 *   - Creating the descriptor pool
 *   - Initializing ImGui for Vulkan + GLFW
 *   - Beginning ImGui frames and rendering them into a command buffer
 *   - Optionally displaying a debug window with stats and toggles
 *
 * This keeps your main Renderer class free from directly 


 ImGui code.
 */
class UIRenderer
{
public:
    UIRenderer();
    ~UIRenderer();

    /**
     * Initializes ImGui for Vulkan and GLFW, including creating a descriptor pool,
     * setting up ImGui contexts, and uploading fonts.
     *
     * @param context    Pointer to your VulkanContext (device, instance, queue, etc.)
     * @param window     Pointer to your Window class so we can get the GLFWwindow
     * @param renderPass The render pass where we'll record ImGui draw commands
     * @param imageCount The swapchain image count (used by ImGui to manage frames)
     */
    void init(VulkanContext* context, Window* window, VkRenderPass renderPass, uint32_t imageCount);

    /**
     * Cleans up all ImGui resources, shutting down the context,
     * destroying descriptor pool, etc.
     */
    void cleanup();

    /**
     * Begins a new ImGui frame (calls ImGui_ImplVulkan_NewFrame, ImGui_ImplGlfw_NewFrame, etc.)
     * Must be called each frame before rendering or displaying any ImGui widgets.
     */
    void beginFrame();

    /**
     * Finishes the current ImGui frame and records all draw data into the provided command buffer.
     *
     * @param cmdBuf The Vulkan command buffer to which ImGui draw calls will be recorded.
     */
    void renderImGui(VkCommandBuffer cmdBuf);

    /**
     * Renders a "Debug" window that displays various stats (FPS, CPU usage, etc.)
     * and allows toggling wireframe, culling, and mesher type.
     *
     * The data is passed from your main renderer or logic, so that the UI is
     * effectively data-driven rather than pulling from global variables.
     *
     * @param dt                 Delta time this frame
     * @param fps                Instant FPS
     * @param avgFps             Rolling-average FPS
     * @param cpuUsage           Instant CPU usage percentage
     * @param avgCpu             Rolling-average CPU usage
     * @param totalVertices      The total vertex count in the scene
     * @param drawCallCount      The total draw calls for the frame
     * @param voxelWorld         Pointer to the voxel world (used for mesher toggles, chunk stats)
     * @param wireframeOn        Reference to a boolean controlling wireframe mode
     * @param enableFrustumCulling Reference to a boolean controlling frustum culling
     */
    void renderDebugWindow(
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
    );

private:
    VulkanContext* m_context = nullptr;
    VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
    bool             m_initialized = false;
};
