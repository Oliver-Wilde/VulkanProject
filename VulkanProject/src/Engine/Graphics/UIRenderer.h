#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

class UIRenderer
{
public:
    UIRenderer();
    ~UIRenderer();

    // Updated to accept a GLFWwindow* and a VkCommandPool for font upload.
    void init(VkInstance instance,
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        uint32_t queueFamily,
        VkQueue queue,
        VkDescriptorPool descriptorPool,
        VkRenderPass renderPass,
        uint32_t imageCount,
        GLFWwindow* window,
        VkCommandPool cmdPool);

    void render(VkCommandBuffer cmdBuf);
    void shutdown();

    void update();

private:
    // Add any internal state if needed.
};
