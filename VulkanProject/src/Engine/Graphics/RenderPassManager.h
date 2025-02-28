#pragma once

#include <vulkan/vulkan.h>
#include <vector>

class VulkanContext;
class SwapChain;

/**
 * Manages the creation of a Vulkan render pass and associated framebuffers.
 */
class RenderPassManager
{
public:
    RenderPassManager(VulkanContext* context, SwapChain* swapChain);
    ~RenderPassManager();

    /**
     * Creates the render pass with color + depth attachments.
     */
    void createRenderPass();

    /**
     * Creates one framebuffer per swapchain image.
     */
    void createFramebuffers();

    /**
     * Cleans up (destroys) the framebuffers, depth image+view+memory, and the render pass.
     * This allows the renderer to re-create them during a swapchain resize.
     */
    void cleanup();

    VkRenderPass getRenderPass() const { return m_renderPass; }
    const std::vector<VkFramebuffer>& getFramebuffers() const { return m_framebuffers; }

private:
    void createDepthResources();
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

private:
    VulkanContext* m_context = nullptr;
    SwapChain* m_swapChain = nullptr;

    VkRenderPass   m_renderPass = VK_NULL_HANDLE;

    // Depth attachment resources
    VkImage        m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView    m_depthImageView = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> m_framebuffers;

    // We pick a single hardcoded depth format for simplicity
    const VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;
};
