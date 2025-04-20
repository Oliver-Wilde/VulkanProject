#pragma once
// ============================================================================
// SwapChain.h  – updated to match the new implementation
//   • Adds an internal `m_images` array so SwapChain.cpp compiles.
//   • No public API changed, so other engine code remains untouched.
// ============================================================================

#include <vulkan/vulkan.h>
#include <vector>

// forward declarations -------------------------------------------------------
class VulkanContext;
class Window;

/**
 * Thin wrapper around VkSwapchainKHR and its image views.
 */
class SwapChain
{
public:
    SwapChain() = default;
    ~SwapChain() = default;

    /** Initialise swap chain + image views. */
    void init(VulkanContext* context, Window* window);
    /** Destroy all swap?chain?related Vulkan resources. */
    void cleanup();

    // ------------------------------------------------------------------------
    // Getters
    // ------------------------------------------------------------------------
    VkSwapchainKHR               getSwapChain()  const { return m_swapChain; }
    VkFormat                     getColorFormat() const { return m_colorFormat; }
    VkExtent2D                   getExtent()      const { return m_extent; }
    const std::vector<VkImageView>& getImageViews() const { return m_imageViews; }
    uint32_t                     getImageCount()  const
    {
        return static_cast<uint32_t>(m_imageViews.size());
    }

private:
    // helper functions (defined in SwapChain.cpp)
    void createSwapChain(Window* window);
    void createImageViews();

    // ------------------------------------------------------------------------
    // Data members
    // ------------------------------------------------------------------------
    VulkanContext* m_context = nullptr;
    VkSwapchainKHR            m_swapChain = VK_NULL_HANDLE;
    VkFormat                  m_colorFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D                m_extent = { 0, 0 };

    std::vector<VkImage>      m_images;      // <?? added
    std::vector<VkImageView>  m_imageViews;
};
