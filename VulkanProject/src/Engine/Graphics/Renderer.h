#pragma once

#include <vulkan/vulkan.h>
#include <glm/mat4x4.hpp>
#include <vector>
#include <string>
#include <deque>

#include "Engine/Scene/Camera.h"
#include "Engine/Voxels/VoxelWorld.h"

class VulkanContext;
class Window;
class ResourceManager;
class PipelineManager;
class RenderPassManager;
class Time;

/**
 * A small struct for the MVP uniform buffer block.
 */
struct MVPBlock
{
    glm::mat4 mvp;
};

/**
 * For double-buffering or triple-buffering, we store per-frame objects:
 * - command buffer
 * - semaphores
 * - fence
 */
struct FrameData
{
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore     imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore     renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence         inFlightFence = VK_NULL_HANDLE;
};

/**
 * The Renderer class handles:
 *  - Creating the SwapChain
 *  - Creating RenderPass & Framebuffers
 *  - Setting up pipelines (fill / wireframe)
 *  - Recording command buffers to draw voxel chunks
 *  - Managing ImGui
 *  - Recreating resources on window resize
 *  - Now includes multi-LOD usage
 */
class Renderer
{
public:
    Renderer(VulkanContext* context, Window* window, VoxelWorld* voxelWorld);
    ~Renderer();

    void setTime(Time* time) { m_time = time; }
    void setCamera(const Camera& cam);

    /**
     * Toggle wireframe on/off. Switches between "voxel_wireframe" pipeline and "voxel_fill".
     */
    void toggleWireframe();

    /**
     * The main per-frame rendering function.
     */
    void renderFrame();

private:
    // Creates MVP uniform buffer & descriptor set
    void createMVPUniformBuffer();
    void updateMVP();
    void recreateSwapChain();

    // Creates a generic GPU buffer + memory
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory
    );

    // Helper for computing a rolling average of FPS, CPU usage, etc.
    void addSample(std::deque<float>& buffer, float value);
    static float computeAverage(const std::deque<float>& buffer);

    // Finds a suitable memory type for the buffer
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

private:
    static const int MAX_FRAMES_IN_FLIGHT = 2;
    static const int ROLLING_AVG_SAMPLES = 60;

    VulkanContext* m_context = nullptr;
    Window* m_window = nullptr;
    VoxelWorld* m_voxelWorld = nullptr;
    Time* m_time = nullptr;

    class SwapChain* m_swapChain = nullptr;
    ResourceManager* m_resourceMgr = nullptr;
    PipelineManager* m_pipelineMgr = nullptr;
    RenderPassManager* m_rpManager = nullptr;

    // For ImGui
    VkDescriptorPool     m_imguiDescriptorPool = VK_NULL_HANDLE;

    // MVP Uniform + Descriptor
    VkBuffer             m_mvpBuffer = VK_NULL_HANDLE;
    VkDeviceMemory       m_mvpMemory = VK_NULL_HANDLE;
    VkDescriptorPool     m_mvpDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mvpLayout = VK_NULL_HANDLE;
    VkDescriptorSet       m_mvpDescriptorSet = VK_NULL_HANDLE;

    // Are we in wireframe mode?
    bool m_wireframeOn = false;
    // Are we culling with a frustum?
    bool m_enableFrustumCulling = false;

    // Rolling average samples (for FPS, CPU usage)
    std::deque<float> m_fpsSamples;
    std::deque<float> m_cpuSamples;

    // The current camera
    Camera m_camera;

    // Per-frame data
    FrameData m_frames[MAX_FRAMES_IN_FLIGHT];
    int       m_currentFrame = 0;
};

