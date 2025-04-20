// ============================================================================
// SwapChain.cpp  – C++14‑friendly version
//   • Replaces std::clamp with manual min/max logic
//   • Removes the unused getGraphicsFamilyIndex() call
//   • **2025‑04‑20:** Default surface format changed to *UNORM*
// ============================================================================

#include "SwapChain.h"
#include "VulkanContext.h"
#include "Engine/Core/Window.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vector>
#include <algorithm>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
static VkPresentModeKHR chooseBestPresentMode(
    const std::vector<VkPresentModeKHR>& modes)
{
    for (VkPresentModeKHR m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            return m;
    return VK_PRESENT_MODE_FIFO_KHR;   // always supported
}

// ============================================================================
// public
// ============================================================================
void SwapChain::init(VulkanContext* ctx, Window* window)
{
    m_context = ctx;
    createSwapChain(window);
    createImageViews();
}

void SwapChain::cleanup()
{
    VkDevice dev = m_context->getDevice();
    for (auto view : m_imageViews)
        vkDestroyImageView(dev, view, nullptr);
    m_imageViews.clear();

    if (m_swapChain)
        vkDestroySwapchainKHR(dev, m_swapChain, nullptr);
    m_swapChain = VK_NULL_HANDLE;
}

// ============================================================================
// createSwapChain
// ============================================================================
void SwapChain::createSwapChain(Window* window)
{
    VkPhysicalDevice gpu = m_context->getPhysicalDevice();
    VkSurfaceKHR     surf = m_context->getSurface();

    // Query caps / formats / present modes ----------------------------------
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surf, &caps);

    uint32_t fmtCount = 0, pmCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surf, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surf, &fmtCount, formats.data());

    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surf, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surf,
        &pmCount, presentModes.data());

    // ───────────────────────────────────────────────────────────────────────
    // Choose surface format  – **Prefer linear UNORM, NOT sRGB** (gamma will
    // be applied elsewhere).  Fallback to whatever the driver gave us.
    // ───────────────────────────────────────────────────────────────────────
    VkSurfaceFormatKHR surfaceFmt = formats[0];
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surfaceFmt = f;
            break;
        }

    // Present mode -----------------------------------------------------------
    VkPresentModeKHR present = chooseBestPresentMode(presentModes);

    // Extent (window size clamped to caps) -----------------------------------
    VkExtent2D extent;
    if (caps.currentExtent.width != UINT32_MAX)
    {
        extent = caps.currentExtent;
    }
    else
    {
        int w, h;  glfwGetFramebufferSize(window->getGLFWwindow(), &w, &h);
        extent.width = std::max(caps.minImageExtent.width,
            std::min<uint32_t>(w, caps.maxImageExtent.width));
        extent.height = std::max(caps.minImageExtent.height,
            std::min<uint32_t>(h, caps.maxImageExtent.height));
    }

    // Image count ------------------------------------------------------------
    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount && imgCount > caps.maxImageCount)
        imgCount = caps.maxImageCount;

    // Swap‑chain create info --------------------------------------------------
    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surf;
    ci.minImageCount = imgCount;
    ci.imageFormat = surfaceFmt.format;
    ci.imageColorSpace = surfaceFmt.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;   // fastest
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = present;
    ci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(m_context->getDevice(), &ci, nullptr,
        &m_swapChain) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swap chain!");

    // Retrieve images --------------------------------------------------------
    vkGetSwapchainImagesKHR(m_context->getDevice(), m_swapChain,
        &imgCount, nullptr);
    m_images.resize(imgCount);
    vkGetSwapchainImagesKHR(m_context->getDevice(), m_swapChain,
        &imgCount, m_images.data());

    m_colorFormat = surfaceFmt.format;
    m_extent = extent;
}

// ============================================================================
// createImageViews
// ============================================================================
void SwapChain::createImageViews()
{
    m_imageViews.resize(m_images.size());

    for (size_t i = 0; i < m_images.size(); ++i)
    {
        VkImageViewCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image = m_images[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = m_colorFormat;
        ci.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.baseMipLevel = 0;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.baseArrayLayer = 0;
        ci.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_context->getDevice(), &ci, nullptr,
            &m_imageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create swap‑chain image view!");
    }
}
// ============================================================================
// end of file
// ============================================================================
