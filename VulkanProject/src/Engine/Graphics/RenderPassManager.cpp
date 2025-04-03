#include "RenderPassManager.h"
#include "SwapChain.h"
#include "Engine/Graphics/VulkanContext.h"

#include <array>
#include <stdexcept>

// -----------------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------------
RenderPassManager::RenderPassManager(VulkanContext* context, SwapChain* swapChain)
    : m_context(context)
    , m_swapChain(swapChain)
{
    // By default, m_occlusionExtent is {0,0}.
    // Make sure to call setOcclusionExtent(...) before creating occlusion pass!
}

RenderPassManager::~RenderPassManager()
{
    cleanup();
}

// -----------------------------------------------------------------------------
// createRenderPass => main pass with color + depth
// -----------------------------------------------------------------------------
void RenderPassManager::createRenderPass()
{
    VkFormat swapchainFormat = m_swapChain->getColorFormat();

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {
        colorAttachment, depthAttachment
    };

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpInfo.pAttachments = attachments.data();
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_context->getDevice(), &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create render pass!");
    }
}
void RenderPassManager::createFramebuffers()
{
    VkDevice device = m_context->getDevice();
    auto& imageViews = m_swapChain->getImageViews();
    auto extent = m_swapChain->getExtent();

    createDepthResources();

    m_framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++)
    {
        std::array<VkImageView, 2> attachments = {
            imageViews[i],
            m_depthImageView
        };

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_renderPass;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create framebuffer!");
        }
    }
}
// -----------------------------------------------------------------------------
// createOcclusionRenderPass => depth-only pass (no color attachments)
// -----------------------------------------------------------------------------
void RenderPassManager::createOcclusionRenderPass()
{
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_occlusionDepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pColorAttachments = nullptr;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &depthAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_context->getDevice(), &rpInfo, nullptr, &m_occlusionRenderPass) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create occlusion render pass!");
    }
}
void RenderPassManager::createOcclusionFramebuffers()
{
    VkDevice device = m_context->getDevice();

    // Build an image for occlusion pass
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_occlusionExtent.width;
        imageInfo.extent.height = m_occlusionExtent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_occlusionDepthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(device, &imageInfo, nullptr, &m_occlusionDepthImage) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create occlusion depth image!");
        }

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device, m_occlusionDepthImage, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_occlusionDepthMemory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate occlusion depth memory!");
        }

        vkBindImageMemory(device, m_occlusionDepthImage, m_occlusionDepthMemory, 0);
    }

    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_occlusionDepthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_occlusionDepthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &m_occlusionDepthView) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create occlusion depth image view!");
        }
    }

    m_occlusionFramebuffers.resize(1);

    VkImageView attachments[1] = { m_occlusionDepthView };

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_occlusionRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = attachments;
    fbInfo.width = m_occlusionExtent.width;
    fbInfo.height = m_occlusionExtent.height;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(device, &fbInfo, nullptr, &m_occlusionFramebuffers[0]) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create occlusion framebuffer!");
    }
}
void RenderPassManager::cleanup()
{
    VkDevice device = m_context->getDevice();

    // Destroy main pass framebuffers
    for (auto fb : m_framebuffers) {
        vkDestroyFramebuffer(device, fb, nullptr);
    }
    m_framebuffers.clear();

    // Destroy main pass
    if (m_renderPass) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    // Destroy the main depth imageView
    if (m_depthImageView) {
        vkDestroyImageView(device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }

    // Destroy the main depth image
    if (m_depthImage) {
        vkDestroyImage(device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }

    // Destroy the main depth memory
    if (m_depthMemory) {
        vkFreeMemory(device, m_depthMemory, nullptr);
        m_depthMemory = VK_NULL_HANDLE;
    }

    // ----------------------------------------------------------------
    // NEW: Destroy occlusion pass resources
    // ----------------------------------------------------------------
    for (auto fb : m_occlusionFramebuffers) {
        vkDestroyFramebuffer(device, fb, nullptr);
    }
    m_occlusionFramebuffers.clear();

    if (m_occlusionRenderPass) {
        vkDestroyRenderPass(device, m_occlusionRenderPass, nullptr);
        m_occlusionRenderPass = VK_NULL_HANDLE;
    }

    // If you haven't already done so, free the occlusion depth view
    if (m_occlusionDepthView) {
        vkDestroyImageView(device, m_occlusionDepthView, nullptr);
        m_occlusionDepthView = VK_NULL_HANDLE;
    }

    // Free the occlusion depth image
    if (m_occlusionDepthImage) {
        vkDestroyImage(device, m_occlusionDepthImage, nullptr);
        m_occlusionDepthImage = VK_NULL_HANDLE;
    }

    // Free the occlusion depth memory
    if (m_occlusionDepthMemory) {
        vkFreeMemory(device, m_occlusionDepthMemory, nullptr);
        m_occlusionDepthMemory = VK_NULL_HANDLE;
    }
    // ----------------------------------------------------------------
}
void RenderPassManager::createDepthResources()
{
    VkDevice device = m_context->getDevice();
    auto extent = m_swapChain->getExtent();

    // Depth image for main pass
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &m_depthImage) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create depth image!");
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, m_depthImage, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_depthMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate depth image memory!");
    }

    vkBindImageMemory(device, m_depthImage, m_depthMemory, 0);

    // Create depth image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create depth image view!");
    }
}

uint32_t RenderPassManager::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_context->getPhysicalDevice(), &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((filter & (1 << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}
