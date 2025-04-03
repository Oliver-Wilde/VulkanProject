#pragma once

#include <vulkan/vulkan.h>
#include <vector>

/**
 * Forward declarations
 */
class VulkanContext;
class SwapChain;

/**
 * Manages the creation of Vulkan render passes (main pass, occlusion pass)
 * and associated framebuffers.
 */
class RenderPassManager
{
public:
    RenderPassManager(VulkanContext* context, SwapChain* swapChain);
    ~RenderPassManager();

    /**
     * Creates the main render pass with color + depth attachments.
     * Used for final scene rendering to the swapchain.
     */
    void createRenderPass();

    /**
     * Creates one framebuffer per swapchain image for the main pass.
     * Each framebuffer references the swapchain's color image + our depth image.
     */
    void createFramebuffers();

    /**
     * Cleans up (destroys) all framebuffers, images, image views,
     * and both render passes (main + occlusion).
     * Called when we need to recreate resources (e.g., window resize).
     */
    void cleanup();

    /**
     * Returns the main render pass (color + depth).
     */
    VkRenderPass getRenderPass() const { return m_renderPass; }

    /**
     * Returns the framebuffers for the main pass (one per swapchain image).
     */
    const std::vector<VkFramebuffer>& getFramebuffers() const { return m_framebuffers; }

    // -------------------------------------------------------------------------
    // Occlusion pass (depth-only) support
    // -------------------------------------------------------------------------

    /**
     * Creates a depth-only render pass for occlusion queries (no color attachments).
     */
    void createOcclusionRenderPass();

    /**
     * Creates the depth image + framebuffer(s) used by the occlusion pass.
     * It uses m_occlusionExtent instead of the swapchain extent.
     */
    void createOcclusionFramebuffers();

    /**
     * Returns the occlusion (depth-only) render pass.
     */
    VkRenderPass getOcclusionRenderPass() const { return m_occlusionRenderPass; }

    /**
     * Returns the occlusion pass framebuffer(s).
     * Typically just one if we’re using a single offscreen depth buffer.
     */
    const std::vector<VkFramebuffer>& getOcclusionFramebuffers() const { return m_occlusionFramebuffers; }

    /**
     * Sets the size of the occlusion pass (width, height) in pixels.
     * This should be set before createOcclusionRenderPass() / createOcclusionFramebuffers().
     */
    void setOcclusionExtent(VkExtent2D extent) { m_occlusionExtent = extent; }

private:
    /**
     * Creates the depth image/memory/view for the main pass.
     */
    void createDepthResources();

    /**
     * Utility function to find a suitable memory type index from the GPU.
     */
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

private:
    // -------------------------------------------------------------------------
    // References
    // -------------------------------------------------------------------------
    VulkanContext* m_context = nullptr;
    SwapChain* m_swapChain = nullptr;

    // -------------------------------------------------------------------------
    // Main pass (color + depth)
    // -------------------------------------------------------------------------
    VkRenderPass   m_renderPass = VK_NULL_HANDLE;

    // Depth resources for the main pass
    VkImage        m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView    m_depthImageView = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> m_framebuffers;

    // The main pass depth format (hardcoded).
    const VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;

    // -------------------------------------------------------------------------
    // Occlusion pass (depth-only)
    // -------------------------------------------------------------------------
    VkRenderPass m_occlusionRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_occlusionFramebuffers;

    // Depth resources for occlusion pass
    VkImage        m_occlusionDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_occlusionDepthMemory = VK_NULL_HANDLE;
    VkImageView    m_occlusionDepthView = VK_NULL_HANDLE;

    // Format and extent for the occlusion pass.
    // Typically we can pick a smaller resolution for performance.
    VkFormat   m_occlusionDepthFormat = VK_FORMAT_D32_SFLOAT;
    VkExtent2D m_occlusionExtent = { 256, 256 }; // initial default
};
