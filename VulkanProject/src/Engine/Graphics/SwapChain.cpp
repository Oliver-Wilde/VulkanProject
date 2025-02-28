// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "SwapChain.h"
#include "VulkanContext.h"
#include "Engine/Core/Window.h"  // So we can get the framebuffer size
#include <stdexcept>
#include <vector>
#include <GLFW/glfw3.h>         // For glfwGetFramebufferSize

void SwapChain::init(VulkanContext* context, Window* window)
{
    m_context = context;

    // -------------------------------------------------------------------------
    // 1) Query the actual framebuffer size from the GLFW window
    // -------------------------------------------------------------------------
    int width = 0, height = 0;
    glfwGetFramebufferSize(window->getGLFWwindow(), &width, &height);

    // If the user has the window minimized, width/height can be 0,
    // so we clamp them to at least 1 to avoid errors.
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;

    // -------------------------------------------------------------------------
    // 2) Choose color format (in real code, you'd query surface formats)
    // -------------------------------------------------------------------------
    m_colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    m_extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };

    // -------------------------------------------------------------------------
    // 3) Create the swapchain
    //    (This is placeholder code; in production you'd query for support,
    //     pick minImageCount, presentMode, etc. more dynamically.)
    // -------------------------------------------------------------------------
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_context->getSurface();
    createInfo.minImageCount = 2; // double-buffering
    createInfo.imageFormat = m_colorFormat;
    createInfo.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = m_extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always supported
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    // If your graphics & present queue families differ, set imageSharingMode = VK_SHARING_MODE_CONCURRENT
    // but for now we assume they are the same => VK_SHARING_MODE_EXCLUSIVE
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = nullptr;

    if (vkCreateSwapchainKHR(m_context->getDevice(), &createInfo, nullptr, &m_swapChain) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain!");
    }

    // -------------------------------------------------------------------------
    // 4) Retrieve the swapchain images
    // -------------------------------------------------------------------------
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(m_context->getDevice(), m_swapChain, &imageCount, nullptr);
    std::vector<VkImage> swapChainImages(imageCount);
    vkGetSwapchainImagesKHR(m_context->getDevice(), m_swapChain, &imageCount, swapChainImages.data());

    // -------------------------------------------------------------------------
    // 5) Create image views for each swapchain image
    // -------------------------------------------------------------------------
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
    // Destroy image views
    for (auto view : m_imageViews)
    {
        vkDestroyImageView(m_context->getDevice(), view, nullptr);
    }
    m_imageViews.clear();

    // Destroy swapchain
    if (m_swapChain) {
        vkDestroySwapchainKHR(m_context->getDevice(), m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
}
