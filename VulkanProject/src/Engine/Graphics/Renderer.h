#pragma once

#include <deque>
#include <vector>            // [ADDED] for std::vector
#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>    // For MVPBlock
#include "Engine/Scene/Camera.h"
#include "Engine/Voxels/VoxelWorld.h"  // [ADDED] so we can use QueuedChunkDestruction

// Forward declarations to avoid heavy includes
class Window;
class VulkanContext;
class ResourceManager;
class PipelineManager;
class RenderPassManager;
class Camera;
class Time;
class UIRenderer;

/**
 * A simple struct holding our MVP matrix.
 * This is what's placed in a uniform buffer for shaders.
 */
struct MVPBlock
{
    glm::mat4 mvp;
};

/**
 * Holds resources for each "frame in flight" (2 or 3).
 * This includes command buffers, semaphores, and fences.
 */
struct FrameResources
{
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore     imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore     renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence         inFlightFence = VK_NULL_HANDLE;
};

/// How many frames we keep in flight (e.g., double or triple buffering).
static const int MAX_FRAMES_IN_FLIGHT = 2;

/**
 * The Renderer class is responsible for:
 *   - Creating swap chain & related Vulkan resources
 *   - Managing pipelines, uniform buffers (MVP), etc.
 *   - Providing a renderFrame() function that draws the voxel world and the ImGui UI
 *
 * A separate UIRenderer handles all ImGui logic, and is called here
 * to begin and end ImGui frames.
 */
class Renderer
{
public:
    /**
     * Constructor
     * @param context      Pointer to your VulkanContext (VkDevice, queues, etc.)
     * @param window       Pointer to your Window class
     * @param voxelWorld   Pointer to your VoxelWorld, so we can draw chunks
     */
    Renderer(VulkanContext* context, Window* window, VoxelWorld* voxelWorld);

    /**
     * Destructor
     * Cleans up swap chain, frame resources, and calls UIRenderer::cleanup() if needed.
     */
    ~Renderer();

    /**
     * Render one frame:
     *   - Acquire swap chain image
     *   - Record command buffer (drawing chunks)
     *   - Let UIRenderer handle ImGui UI
     *   - Present the result
     */
    void renderFrame();

    /**
     * Update the camera used for MVP calculations.
     */
    void setCamera(const Camera& cam);

    /**
     * Toggle wireframe pipeline usage (fill vs. polygonMode = line).
     */
    void toggleWireframe();

    /**
     * Provide a pointer to your Time object, if you want to track dt/fps in the renderer.
     */
    void setTime(Time* time);

    // [ADDED] For ring-buffer resource destruction
    void enqueueDeferredDestroy(const QueuedChunkDestruction& qcd);

private:
    /**
     * Creates the uniform buffer for MVP (model-view-projection),
     * plus its descriptor set and pool.
     */
    void createMVPUniformBuffer();

    /**
     * Re-writes the MVP uniform buffer with the current camera and projection.
     */
    void updateMVP();

    /**
     * Recreates the swap chain (and related resources) if the window is resized,
     * or if it becomes out-of-date.
     */
    void recreateSwapChain();

    /**
     * A helper to create a Vulkan buffer (for uniform, vertex, or index).
     */
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory
    );

    /**
     * Picks a memory type from the physical device that matches 'filter' and 'props'.
     */
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

    /**
     * Adds a new sample to our rolling-average buffer (e.g., for FPS or CPU usage).
     */
    void addSample(std::deque<float>& buffer, float value);

    /**
     * Computes the average of all samples in a deque.
     */
    float computeAverage(const std::deque<float>& buffer);

    /**
     * [ADDED] Called at the start of each frame (after waiting on the fence)
     * to free any chunk buffers queued in the previous use of this frame index.
     */
    void freeDeferredResources();

private:
    // Core references
    VulkanContext* m_context = nullptr;
    Window* m_window = nullptr;
    VoxelWorld* m_voxelWorld = nullptr;

    // Manager objects
    ResourceManager* m_resourceMgr = nullptr;
    PipelineManager* m_pipelineMgr = nullptr;
    RenderPassManager* m_rpManager = nullptr;

    /**
     * UIRenderer handles all ImGui logic (init, frame begin, rendering, teardown).
     */
    UIRenderer* m_uiRenderer = nullptr;

    // Swap chain and MVP data
    class SwapChain* m_swapChain = nullptr;
    VkBuffer            m_mvpBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_mvpMemory = VK_NULL_HANDLE;
    VkDescriptorPool    m_mvpDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet     m_mvpDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mvpLayout = VK_NULL_HANDLE;

    // [ADDED] A ring buffer of chunk buffers to free for each frame
    std::vector<QueuedChunkDestruction> m_deferredFrees[MAX_FRAMES_IN_FLIGHT];

    // Per-frame resources
    FrameResources   m_frames[MAX_FRAMES_IN_FLIGHT];
    int              m_currentFrame = 0; // which frame index we're on

    // If you store a pointer to a global Time or create one in Application:
    Time* m_time = nullptr;

    // Toggles
    bool m_wireframeOn = false;
    bool m_enableFrustumCulling = false;

    // Rolling-average data for FPS, CPU usage, etc.
    static const int ROLLING_AVG_SAMPLES = 120;
    std::deque<float> m_fpsSamples;
    std::deque<float> m_cpuSamples;

    // Camera
    Camera m_camera;
};
