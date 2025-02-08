#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>

struct PipelineInfo {
    VkPipeline       pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
};

class VulkanContext;
class ResourceManager;

class PipelineManager {
public:
    PipelineManager(VulkanContext* context, ResourceManager* resourceMgr);
    ~PipelineManager();

    // 1) No-descriptor pipeline (fill mode) - optional if you still want it
    void createVoxelPipeline(
        const std::string& pipelineName,
        VkRenderPass renderPass,
        VkExtent2D viewportExtent);

    // 2) "Fill" pipeline WITH descriptor layout
    void createVoxelPipelineFill(
        const std::string& pipelineName,
        VkRenderPass renderPass,
        VkExtent2D viewportExtent,
        VkDescriptorSetLayout descriptorLayout);

    // 3) "Wireframe" pipeline WITH descriptor layout
    void createVoxelPipelineWireframe(
        const std::string& pipelineName,
        VkRenderPass renderPass,
        VkExtent2D viewportExtent,
        VkDescriptorSetLayout descriptorLayout);

    // Create a descriptor set layout for the MVP uniform
    VkDescriptorSetLayout createMVPDescriptorSetLayout();

    // Retrieve pipeline
    PipelineInfo getPipeline(const std::string& pipelineName);

private:
    // If you need an empty pipeline layout at any time
    VkPipelineLayout createEmptyPipelineLayout();

private:
    VulkanContext* m_context = nullptr;
    ResourceManager* m_resourceMgr = nullptr;
    std::unordered_map<std::string, PipelineInfo> m_pipelines;
};