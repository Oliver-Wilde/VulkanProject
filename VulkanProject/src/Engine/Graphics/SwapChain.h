#pragma once

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include <vulkan/vulkan.h>
#include <vector>

// Forward declarations
class VulkanContext;
class Window;

/**
 * A basic wrapper around VkSwapchainKHR and its associated image views.
 */
class SwapChain
{
public:
    SwapChain() = default;
    ~SwapChain() = default;

    /**
     * Initializes the swapchain using the provided VulkanContext and Window.
     * This will create a VkSwapchainKHR, retrieve the images, and create image views.
     *
     * @param context  The VulkanContext, which should have a valid VkDevice, surface, etc.
     * @param window   The Window so we can query the framebuffer size.
     */
    void init(VulkanContext* context, Window* window);

    /**
     * Cleans up all swapchain-related resources (the VkSwapchainKHR and its image views).
     */
    void cleanup();

    // Getters
    VkSwapchainKHR getSwapChain() const { return m_swapChain; }
    VkFormat       getColorFormat() const { return m_colorFormat; }
    VkExtent2D     getExtent() const { return m_extent; }

    /**
     * @return A list of VkImageView for each swapchain image.
     */
    const std::vector<VkImageView>& getImageViews() const { return m_imageViews; }

    /**
     * @return The number of images in the swapchain.
     */
    uint32_t getImageCount() const
    {
        return static_cast<uint32_t>(m_imageViews.size());
    }

private:
    // Member variables
    VulkanContext* m_context = nullptr;
    VkSwapchainKHR             m_swapChain = VK_NULL_HANDLE;
    VkFormat                   m_colorFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D                 m_extent = { 0, 0 };
    std::vector<VkImageView>   m_imageViews;
};
