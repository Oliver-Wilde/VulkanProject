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
     * This is used for the final scene rendering.
     */
    void createRenderPass();

    /**
     * Creates one framebuffer per swapchain image for the main pass.
     */
    void createFramebuffers();

    /**
     * Cleans up (destroys) all framebuffers, images, image views,
     * and both render passes.
     * This allows the renderer to re-create them during a swapchain resize
     * or resolution change.
     */
    void cleanup();

    /**
     * Returns the main render pass.
     */
    VkRenderPass getRenderPass() const { return m_renderPass; }

    /**
     * Returns the main pass framebuffers (one per swapchain image).
     */
    const std::vector<VkFramebuffer>& getFramebuffers() const { return m_framebuffers; }

    // -------------------------------------------------------------------------
    // OPTIONAL: Support for a separate occlusion pass (depth-only).
    // -------------------------------------------------------------------------

    /**
     * Creates a depth-only render pass for occlusion queries (no color attachments).
     */
    void createOcclusionRenderPass();

    /**
     * Creates a single (or multiple) occlusion pass framebuffer(s) using a
     * smaller depth image at m_occlusionExtent.
     */
    void createOcclusionFramebuffers();

    /**
     * Returns the occlusion render pass.
     */
    VkRenderPass getOcclusionRenderPass() const { return m_occlusionRenderPass; }

    /**
     * Returns the framebuffers used by the occlusion pass.
     */
    const std::vector<VkFramebuffer>& getOcclusionFramebuffers() const { return m_occlusionFramebuffers; }

    /**
     * Set the resolution for the occlusion pass. Typically smaller than swapchain extent.
     */
    void setOcclusionExtent(VkExtent2D extent) { m_occlusionExtent = extent; }

private:
    // Creates the depth image + memory + view for the main pass
    void createDepthResources();

    // Utility to pick memory type
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

private:
    // -------------------------------------------------------------------------
    // References
    // -------------------------------------------------------------------------
    VulkanContext* m_context = nullptr;
    SwapChain* m_swapChain = nullptr;

    // -------------------------------------------------------------------------
    // Main pass
    // -------------------------------------------------------------------------
    VkRenderPass   m_renderPass = VK_NULL_HANDLE;

    // Depth attachment resources (for main pass)
    VkImage        m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView    m_depthImageView = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;

    // We pick a single hardcoded depth format for simplicity
    const VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;

    // -------------------------------------------------------------------------
    // Occlusion pass (optional)
    // -------------------------------------------------------------------------
    VkRenderPass               m_occlusionRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_occlusionFramebuffers;

    // Depth resources for occlusion pass
    VkImage        m_occlusionDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_occlusionDepthMemory = VK_NULL_HANDLE;
    VkImageView    m_occlusionDepthView = VK_NULL_HANDLE;

    // Format and extent for the occlusion pass
    // Could differ from main pass resolution
    VkFormat    m_occlusionDepthFormat = VK_FORMAT_D32_SFLOAT;
    VkExtent2D  m_occlusionExtent = { 256, 256 }; // example default
};
