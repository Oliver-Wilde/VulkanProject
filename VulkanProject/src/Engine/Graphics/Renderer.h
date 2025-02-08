#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "Engine/Scene/Camera.h"

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
class Window;              // We rely on this forward declaration instead of including the header
class VulkanContext;
class SwapChain;
class ResourceManager;
class PipelineManager;
class RenderPassManager;
class VoxelWorld;
class Time;

// -----------------------------------------------------------------------------
// Structs
// -----------------------------------------------------------------------------
/**
 * A simple uniform block holding our MVP matrix.
 */
struct MVPBlock {
    glm::mat4 mvp;
};

/**
 * Holds per-frame Vulkan resources, used in the "frames in flight" approach.
 * Each frame has its own command buffer, semaphores, and fence.
 */
struct FrameResources {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore     imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore     renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence         inFlightFence = VK_NULL_HANDLE;
};

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class Renderer
{
public:
    // -------------------------------------------------------------------------
    // Constructor / Destructor
    // -------------------------------------------------------------------------
    Renderer(VulkanContext* context, Window* window);
    ~Renderer();

    // -------------------------------------------------------------------------
    // Rendering and Updates
    // -------------------------------------------------------------------------
    /**
     * Renders one frame. Acquires a swapchain image, records command buffers,
     * submits, and presents.
     */
    void renderFrame();

    /**
     * Sets the camera used for rendering (updates the internal camera state).
     */
    void setCamera(const Camera& cam);

    /**
     * Toggles the wireframe pipeline vs. the fill pipeline.
     */
    void toggleWireframe();

    /**
     * Sets the Time pointer (so we can retrieve delta time, etc.).
     */
    void setTime(Time* timePtr) { m_time = timePtr; }

    // -------------------------------------------------------------------------
    // Frustum Culling Toggle
    // -------------------------------------------------------------------------
    /**
     * Enables or disables frustum culling for chunk rendering.
     * @param enabled True to enable culling, false to disable.
     */
    void setFrustumCullingEnabled(bool enabled) { m_enableFrustumCulling = enabled; }

    /**
     * @return True if frustum culling is currently enabled, false otherwise.
     */
    bool isFrustumCullingEnabled() const { return m_enableFrustumCulling; }

private:
    // -------------------------------------------------------------------------
    // Internal Helper Methods
    // -------------------------------------------------------------------------
    void createMVPUniformBuffer();
    void updateMVP();

    /**
     * Helper to create a Vulkan buffer + memory allocation.
     */
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory
    );

    /**
     * Finds a suitable memory type index for a buffer.
     */
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

private:
    // -------------------------------------------------------------------------
    // Member Variables
    // -------------------------------------------------------------------------
    VulkanContext* m_context = nullptr; ///< Pointer to the Vulkan context
    Window* m_window = nullptr; ///< Pointer to the window

    SwapChain* m_swapChain = nullptr; ///< Manages the swap chain
    ResourceManager* m_resourceMgr = nullptr; ///< Manages shader modules, etc.
    PipelineManager* m_pipelineMgr = nullptr; ///< Manages graphics pipelines
    RenderPassManager* m_rpManager = nullptr; ///< Manages render passes
    VoxelWorld* m_voxelWorld = nullptr; ///< Voxel-based world pointer

    // MVP Data
    VkBuffer             m_mvpBuffer = VK_NULL_HANDLE;
    VkDeviceMemory       m_mvpMemory = VK_NULL_HANDLE;
    VkDescriptorPool     m_mvpDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mvpLayout = VK_NULL_HANDLE;
    VkDescriptorSet      m_mvpDescriptorSet = VK_NULL_HANDLE;

    // ImGui descriptor pool
    VkDescriptorPool     m_imguiDescriptorPool = VK_NULL_HANDLE;

    // Number of frames we can process simultaneously
    static const int MAX_FRAMES_IN_FLIGHT = 2;

    // Per-frame resources (command buffer, semaphores, fence)
    FrameResources m_frames[MAX_FRAMES_IN_FLIGHT];

    // Which frame index we're currently on
    uint32_t       m_currentFrame = 0;

    // Internal flags / data
    bool   m_wireframeOn = false; ///< Whether wireframe mode is on
    bool   m_enableFrustumCulling = false; ///< Whether frustum culling is enabled
    Camera m_camera;
    Time* m_time = nullptr;
};
