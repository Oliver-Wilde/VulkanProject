// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "SwapChain.h"
#include "VulkanContext.h"

#include <stdexcept>
#include <vector>

// -----------------------------------------------------------------------------
// Public Methods
// -----------------------------------------------------------------------------
void SwapChain::init(VulkanContext* context)
{
    m_context = context;

    // -----------------------------------------------------------------------------
    // 1) Choose color format & extent
    //    (In real code, you'd query surface capabilities, pick the best format, etc.)
    // -----------------------------------------------------------------------------
    m_colorFormat = VK_FORMAT_B8G8R8A8_UNORM; // A common choice
    m_extent = { 800, 600 }; // Placeholder; in real code, read from surface caps

    // -----------------------------------------------------------------------------
    // 2) Create the swapchain
    //    (Placeholder code—normally uses vkCreateSwapchainKHR)
    // -----------------------------------------------------------------------------
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_context->getSurface();
    createInfo.minImageCount = 2; // Double-buffering
    createInfo.imageFormat = m_colorFormat;
    createInfo.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = m_extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // Guaranteed to be supported
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    // Pick a queue family for sharing if needed
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = nullptr;

    if (vkCreateSwapchainKHR(m_context->getDevice(), &createInfo, nullptr, &m_swapChain) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain!");
    }

    // -----------------------------------------------------------------------------
    // 3) Get images from the swapchain
    // -----------------------------------------------------------------------------
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(m_context->getDevice(), m_swapChain, &imageCount, nullptr);
    std::vector<VkImage> swapChainImages(imageCount);
    vkGetSwapchainImagesKHR(m_context->getDevice(), m_swapChain, &imageCount, swapChainImages.data());

    // -----------------------------------------------------------------------------
    // 4) Create image views for each swapchain image
    // -----------------------------------------------------------------------------
    m_imageViews.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapChainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_colorFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_context->getDevice(), &viewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create image view for swapchain image!");
        }
    }
}

void SwapChain::cleanup()
{
    // -----------------------------------------------------------------------------
    // Destroy image views
    // -----------------------------------------------------------------------------
    for (auto view : m_imageViews)
    {
        vkDestroyImageView(m_context->getDevice(), view, nullptr);
    }
    m_imageViews.clear();

    // -----------------------------------------------------------------------------
    // Destroy swapchain
    // -----------------------------------------------------------------------------
    if (m_swapChain) {
        vkDestroySwapchainKHR(m_context->getDevice(), m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
}
