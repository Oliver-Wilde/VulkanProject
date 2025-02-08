#pragma once

#include <vulkan/vulkan.h>
#include <vector>

class VulkanContext;
class SwapChain;

class RenderPassManager
{
public:
    RenderPassManager(VulkanContext* context, SwapChain* swapChain);
    ~RenderPassManager();

    VkRenderPass getRenderPass() const { return m_renderPass; }
    const std::vector<VkFramebuffer>& getFramebuffers() const { return m_framebuffers; }

    void createRenderPass();
    void createFramebuffers();

private:
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props);

    VulkanContext* m_context;
    SwapChain* m_swapChain;


    VkImage         m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory  m_depthMemory = VK_NULL_HANDLE;
    VkImageView     m_depthImageView = VK_NULL_HANDLE;
    VkFormat        m_depthFormat = VK_FORMAT_D32_SFLOAT;  // Common choice
    



    VkRenderPass                 m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>   m_framebuffers;

    void createDepthResources();
};
