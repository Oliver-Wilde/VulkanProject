#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <vector>

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
class VulkanContext;
class SwapChain;

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class RenderPassManager
{
public:
    // -----------------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------------
    RenderPassManager(VulkanContext* context, SwapChain* swapChain);
    ~RenderPassManager();

    // -----------------------------------------------------------------------------
    // Getters
    // -----------------------------------------------------------------------------
    VkRenderPass getRenderPass() const { return m_renderPass; }
    const std::vector<VkFramebuffer>& getFramebuffers() const { return m_framebuffers; }

    // -----------------------------------------------------------------------------
    // Public Methods
    // -----------------------------------------------------------------------------
    void createRenderPass();
    void createFramebuffers();

private:
    // -----------------------------------------------------------------------------
    // Private Methods
    // -----------------------------------------------------------------------------
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);
    void createDepthResources();

    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    VulkanContext* m_context = nullptr;
    SwapChain* m_swapChain = nullptr;

    // Depth Resources
    VkImage        m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView    m_depthImageView = VK_NULL_HANDLE;
    VkFormat       m_depthFormat = VK_FORMAT_D32_SFLOAT;  // Common depth format

    // Render Pass & Framebuffers
    VkRenderPass                m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>  m_framebuffers;
};
