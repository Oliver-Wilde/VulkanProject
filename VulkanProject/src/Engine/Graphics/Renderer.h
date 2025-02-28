#pragma once

#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>
#include <vector>
#include <string>
#include <deque> // For std::deque usage

// Include full Camera definition so we can store it by value:
#include "Engine/Scene/Camera.h" 

// Forward declarations
class VulkanContext;
class Window;
class ResourceManager;
class PipelineManager;
class RenderPassManager;
class VoxelWorld;
class Time;

/**
 * A small struct for the MVP uniform buffer block.
 */
struct MVPBlock
{
    glm::mat4 mvp;
};

/**
 * A small struct holding the objects needed per frame:
 * - A command buffer
 * - Semaphores for image-available and render-finished
 * - A fence to ensure GPU completion
 */
struct FrameData
{
    VkCommandBuffer   commandBuffer = VK_NULL_HANDLE;
    VkSemaphore       imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore       renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence           inFlightFence = VK_NULL_HANDLE;
};

/**
 * The Renderer class handles:
 *   - Creating the SwapChain
 *   - Creating the RenderPass & Framebuffers
 *   - Setting up pipelines (fill / wireframe)
 *   - Recording command buffers to draw voxel chunks
 *   - Managing ImGui integration
 *   - Recreating resources on window resize
 */
class Renderer
{
public:
    /**
     * Constructor: creates the swapchain, render pass, pipelines, etc.
     * @param context     The VulkanContext (must be initialized).
     * @param window      The Window pointer for size queries and ImGui usage.
     * @param voxelWorld  A pointer to the VoxelWorld, so we can draw it.
     */
    Renderer(VulkanContext* context, Window* window, VoxelWorld* voxelWorld);

    /**
     * Destructor: cleans up Vulkan resources in the proper order.
     */
    ~Renderer();

    /**
     * Sets the Time pointer so we can query deltaTime, for debugging display (FPS).
     */
    void setTime(Time* time) { m_time = time; }

    /**
     * Sets the camera so we can build MVP to pass to the shaders.
     */
    void setCamera(const Camera& cam);

    /**
     * Toggles wireframe rendering on/off.
     */
    void toggleWireframe();

    /**
     * The per-frame render function: acquires a swapchain image, records
     * and submits a command buffer, then presents the image.
     */
    void renderFrame();

private:
    /**
     * Creates the MVP uniform buffer and descriptor set for passing the MVP matrix.
     */
    void createMVPUniformBuffer();

    /**
     * Helper to update the MVP uniform buffer each frame.
     */
    void updateMVP();

    /**
     * Recreates the swapchain and all dependent resources when the window size changes
     * or if the swapchain becomes invalid.
     */
    void recreateSwapChain();

    /**
     * A helper to create a buffer (vertex, index, uniform, etc.),
     * allocate memory, and bind them together.
     */
    void createBuffer(VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory);

    /**
     * Adds a new sample to a deque (removing the oldest if we exceed ROLLING_AVG_SAMPLES).
     */
    void addSample(std::deque<float>& buffer, float value);

    /**
     * Computes the average of the values in a std::deque<float>.
     */
    static float computeAverage(const std::deque<float>& buffer);

    /**
     * Finds a suitable memory type on the GPU that matches the filter and properties.
     */
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

private:
    // Constants
    static const int MAX_FRAMES_IN_FLIGHT = 2;

    // You may want an extra constant for your rolling average buffer size:
    static const int ROLLING_AVG_SAMPLES = 60;

    // The VulkanContext provides instance, device, queues, etc.
    VulkanContext* m_context = nullptr;

    // The Window for size queries and ImGui setup
    Window* m_window = nullptr;

    // A pointer to the Time system for reading dt/fps (optional)
    Time* m_time = nullptr;

    // The swapchain we create for drawing to the screen
    class SwapChain* m_swapChain = nullptr;

    // Resource + pipeline managers
    ResourceManager* m_resourceMgr = nullptr;
    PipelineManager* m_pipelineMgr = nullptr;
    RenderPassManager* m_rpManager = nullptr;

    // The voxel world to draw
    VoxelWorld* m_voxelWorld = nullptr;

    // ImGui descriptor pool
    VkDescriptorPool m_imguiDescriptorPool = VK_NULL_HANDLE;

    // MVP uniform buffer
    VkBuffer             m_mvpBuffer = VK_NULL_HANDLE;
    VkDeviceMemory       m_mvpMemory = VK_NULL_HANDLE;
    VkDescriptorPool     m_mvpDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mvpLayout = VK_NULL_HANDLE;
    VkDescriptorSet       m_mvpDescriptorSet = VK_NULL_HANDLE;

    // Whether wireframe mode is on
    bool m_wireframeOn = false;

    // Whether to use frustum culling
    bool m_enableFrustumCulling = false;

    // Rolling average containers for FPS, CPU usage, or anything else
    std::deque<float> m_fpsSamples;
    std::deque<float> m_cpuSamples;

    // Camera
    Camera m_camera;

    // Each frame in flight has its own command buffer, semaphores, fence, etc.
    FrameData m_frames[MAX_FRAMES_IN_FLIGHT];

    // Current frame index (0 or 1 when using double-buffering)
    int m_currentFrame = 0;
};
