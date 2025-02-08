#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "Engine/Scene/Camera.h"

// We need to know "Window" to have Window* as a member.
// Either forward-declare the class or #include its header:
class Window;              // Forward declaration of Window

class VulkanContext;
class SwapChain;
class ResourceManager;
class PipelineManager;
class RenderPassManager;
class VoxelWorld;
class Time;

// A simple uniform block holding our MVP matrix
struct MVPBlock {
    glm::mat4 mvp;
};

class Renderer
{
public:
    // One constructor signature: (VulkanContext*, Window*)
    Renderer(VulkanContext* context, Window* window);
    ~Renderer();

    void renderFrame();
    void setCamera(const Camera& cam);
    void toggleWireframe();

    void setTime(Time* timePtr) { m_time = timePtr; }

private:
    // Helpers
    void createMVPUniformBuffer();
    void updateMVP();
    void createBuffer(VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& bufferMemory);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

private:
    // We keep only ONE declaration of these members

    VulkanContext* m_context = nullptr;
    Window* m_window = nullptr;   // We rely on "class Window;" above

    SwapChain* m_swapChain = nullptr;
    ResourceManager* m_resourceMgr = nullptr;
    PipelineManager* m_pipelineMgr = nullptr;
    RenderPassManager* m_rpManager = nullptr;
    VoxelWorld* m_voxelWorld = nullptr;

    // MVP data
    VkBuffer             m_mvpBuffer = VK_NULL_HANDLE;
    VkDeviceMemory       m_mvpMemory = VK_NULL_HANDLE;
    VkDescriptorPool     m_mvpDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_mvpLayout = VK_NULL_HANDLE;
    VkDescriptorSet      m_mvpDescriptorSet = VK_NULL_HANDLE;

    // Synchronization objects (semaphores)
    VkSemaphore          m_imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore          m_renderFinishedSemaphore = VK_NULL_HANDLE;

    // ImGui descriptor pool
    VkDescriptorPool     m_imguiDescriptorPool = VK_NULL_HANDLE;

    // Internal flags, data
    bool                 m_wireframeOn = false;
    Camera               m_camera;
    Time* m_time = nullptr;
};
