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

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class Renderer
{
public:
    // -----------------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------------
    Renderer(VulkanContext* context, Window* window);
    ~Renderer();

    // -----------------------------------------------------------------------------
    // Public Methods
    // -----------------------------------------------------------------------------
    void renderFrame();
    void setCamera(const Camera& cam);
    void toggleWireframe();

    // A simple setter for the Time pointer
    void setTime(Time* timePtr) { m_time = timePtr; }

private:
    // -----------------------------------------------------------------------------
    // Private Helper Methods
    // -----------------------------------------------------------------------------
    void createMVPUniformBuffer();
    void updateMVP();
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory
    );
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    VulkanContext* m_context = nullptr;     // Pointer to the Vulkan context
    Window* m_window = nullptr;      // Pointer to the Window class

    SwapChain* m_swapChain = nullptr;   // Handles the swap chain
    ResourceManager* m_resourceMgr = nullptr; // Manages resources
    PipelineManager* m_pipelineMgr = nullptr; // Manages pipelines
    RenderPassManager* m_rpManager = nullptr;   // Handles render passes
    VoxelWorld* m_voxelWorld = nullptr;  // Voxel-based world

    // MVP Data
    VkBuffer            m_mvpBuffer = VK_NULL_HANDLE;
    VkDeviceMemory      m_mvpMemory = VK_NULL_HANDLE;
    VkDescriptorPool    m_mvpDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mvpLayout = VK_NULL_HANDLE;
    VkDescriptorSet     m_mvpDescriptorSet = VK_NULL_HANDLE;

    // Synchronization objects (semaphores)
    VkSemaphore         m_imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore         m_renderFinishedSemaphore = VK_NULL_HANDLE;

    // ImGui descriptor pool
    VkDescriptorPool    m_imguiDescriptorPool = VK_NULL_HANDLE;

    // Internal flags / data
    bool                m_wireframeOn = false;
    Camera              m_camera;
    Time* m_time = nullptr;
};
