#pragma once

#include <vulkan/vulkan.h>
#include <vector>

class VulkanContext;

class SwapChain
{
public:
    SwapChain() = default;
    ~SwapChain() = default;

    // Initializes the swapchain (in real code, you'd do vkCreateSwapchainKHR, etc.)
    void init(VulkanContext* context);
    void cleanup();

    // Accessors used by RenderPassManager / Renderer
    VkSwapchainKHR getSwapChain()    const { return m_swapChain; }
    VkFormat       getColorFormat()  const { return m_colorFormat; }
    VkExtent2D     getExtent()       const { return m_extent; }
    const std::vector<VkImageView>& getImageViews() const { return m_imageViews; }
    
    uint32_t getImageCount() const
    {
        // Return how many swapchain images we have
        return static_cast<uint32_t>(m_imageViews.size());
    }


private:
    VulkanContext* m_context = nullptr;
    VkSwapchainKHR  m_swapChain = VK_NULL_HANDLE;

    // For creating a render pass, we need a color format
    VkFormat        m_colorFormat = VK_FORMAT_UNDEFINED;
    // The swapchain size
    VkExtent2D      m_extent = { 0, 0 };
    // Image views for each swapchain image
    std::vector<VkImageView> m_imageViews;
};
