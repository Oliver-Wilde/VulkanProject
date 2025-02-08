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

// -----------------------------------------------------------------------------
// Class Definition
// -----------------------------------------------------------------------------
class SwapChain
{
public:
    // -----------------------------------------------------------------------------
    // Constructor / Destructor
    // -----------------------------------------------------------------------------
    SwapChain() = default;
    ~SwapChain() = default;

    // -----------------------------------------------------------------------------
    // Public Methods
    // -----------------------------------------------------------------------------
    /**
     * Initializes the swapchain (in real code, you'd do vkCreateSwapchainKHR, etc.)
     */
    void init(VulkanContext* context);

    /**
     * Cleans up all swapchain-related resources.
     */
    void cleanup();

    // -----------------------------------------------------------------------------
    // Getters
    // -----------------------------------------------------------------------------
    VkSwapchainKHR getSwapChain() const { return m_swapChain; }
    VkFormat getColorFormat()    const { return m_colorFormat; }
    VkExtent2D getExtent()       const { return m_extent; }
    const std::vector<VkImageView>& getImageViews() const { return m_imageViews; }

    /**
     * @return How many swapchain images we have.
     */
    uint32_t getImageCount() const
    {
        return static_cast<uint32_t>(m_imageViews.size());
    }

private:
    // -----------------------------------------------------------------------------
    // Member Variables
    // -----------------------------------------------------------------------------
    VulkanContext* m_context = nullptr;
    VkSwapchainKHR       m_swapChain = VK_NULL_HANDLE;

    // For creating a render pass, we need a color format.
    VkFormat             m_colorFormat = VK_FORMAT_UNDEFINED;

    // The swapchain size.
    VkExtent2D           m_extent = { 0, 0 };

    // Image views for each swapchain image.
    std::vector<VkImageView> m_imageViews;
};
